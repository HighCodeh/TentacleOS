// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "nfc_mfc_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_manager.h"

#include "mf_classic.h"
#include "nfc_flipper_file.h"
#include "mf_key_cache.h"
#include "mf_key_dict.h"
#include "nfc_poller.h"
#include "poller.h"
#include "storage_mkdir.h"
#include "tos_storage_paths.h"

static const char *TAG = "NFC_MFC_DUMP";

#define RECOVERY_PASSES      3
#define FILE_MODE            0600
#define KEY_B_OFFSET         10
#define CUID_SIZE            4
#define QUICK_DICT_KEYS      5
#define AUTH_QUICK_ATTEMPTS  2
#define AUTH_READ_ATTEMPTS   3
#define AUTH_RETRY_DELAY_MS  20
#define RADIO_START_DELAY_MS 200
#define PREFERRED_KEY_MAX    12
#define DUMP_DIR             TOS_PATH_NFC "/dumps"

int nfc_mfc_dump_first_block(int sector) {
  return sector < MF_4K_BIG_SECTOR_START
             ? sector * MF_SMALL_BLOCK_COUNT
             : MF_4K_BIG_BLOCK_BASE + (sector - MF_4K_BIG_SECTOR_START) * MF_4K_BIG_BLOCK_COUNT;
}

int nfc_mfc_dump_sector_blocks(int sector) {
  return sector < MF_4K_BIG_SECTOR_START ? MF_SMALL_BLOCK_COUNT : MF_4K_BIG_BLOCK_COUNT;
}

int nfc_mfc_dump_block_sector(int block) {
  return block < MF_4K_BIG_BLOCK_BASE
             ? block / MF_SMALL_BLOCK_COUNT
             : MF_4K_BIG_SECTOR_START + (block - MF_4K_BIG_BLOCK_BASE) / MF_4K_BIG_BLOCK_COUNT;
}

typedef struct {
  uint8_t data[PREFERRED_KEY_MAX][NFC_MFC_KEY_SIZE];
  int count;
} preferred_keys_t;

typedef struct {
  nfc_iso14443a_data_t *card;
  nfc_mfc_dump_result_t *out;
  nfc_mfc_dump_cancel_cb_t cancel_cb;
  nfc_mfc_dump_progress_cb_t progress_cb;
  void *ctx;
  int dict_count;
  preferred_keys_t preferred_a;
  preferred_keys_t preferred_b;
} dump_ctx_t;

static bool cancelled(const dump_ctx_t *dc) {
  return dc->cancel_cb != NULL && dc->cancel_cb(dc->ctx);
}

static void report_progress(const dump_ctx_t *dc, nfc_mfc_dump_phase_t phase, int sector) {
  if (dc->progress_cb == NULL)
    return;
  const nfc_mfc_dump_progress_t p = {
      .phase = phase,
      .sector = sector,
      .blocks_read = dc->out->blocks_read,
      .keys_found = dc->out->keys_a_found + dc->out->keys_b_found,
      .sector_count = dc->out->sector_count,
      .total_blocks = dc->out->total_blocks,
  };
  dc->progress_cb(&p, dc->ctx);
}

static bool key_equal(const uint8_t a[NFC_MFC_KEY_SIZE], const uint8_t b[NFC_MFC_KEY_SIZE]) {
  return memcmp(a, b, NFC_MFC_KEY_SIZE) == 0;
}

static bool preferred_contains(const preferred_keys_t *keys, const uint8_t key[NFC_MFC_KEY_SIZE]) {
  for (int i = 0; i < keys->count; i++) {
    if (key_equal(keys->data[i], key))
      return true;
  }
  return false;
}

static void preferred_add(preferred_keys_t *keys, const uint8_t key[NFC_MFC_KEY_SIZE]) {
  if (preferred_contains(keys, key) || keys->count >= PREFERRED_KEY_MAX)
    return;
  memcpy(keys->data[keys->count++], key, NFC_MFC_KEY_SIZE);
}

static hb_nfc_err_t auth_key(dump_ctx_t *dc,
                             uint8_t block,
                             mf_key_type_t type,
                             const uint8_t key_bytes[NFC_MFC_KEY_SIZE],
                             int attempts) {
  mf_classic_key_t key = {0};
  memcpy(key.data, key_bytes, NFC_MFC_KEY_SIZE);
  hb_nfc_err_t last = HB_NFC_ERR_AUTH;

  for (int attempt = 0; attempt < attempts; attempt++) {
    if (cancelled(dc))
      return ESP_ERR_INVALID_STATE;
    if (attempt > 0)
      vTaskDelay(pdMS_TO_TICKS(AUTH_RETRY_DELAY_MS));

    mf_classic_reset_auth();
    last = iso14443a_poller_reselect(dc->card);
    if (last == HB_NFC_OK)
      last = mf_classic_auth(block, type, &key, dc->card->uid + dc->card->uid_len - CUID_SIZE);
    if (last == HB_NFC_OK)
      return HB_NFC_OK;
  }
  return last;
}

static void
remember_key(dump_ctx_t *dc, int sector, mf_key_type_t type, const uint8_t key[NFC_MFC_KEY_SIZE]) {
  bool *found = type == MF_KEY_A ? &dc->out->key_a_found[sector] : &dc->out->key_b_found[sector];
  uint8_t *dst = type == MF_KEY_A ? dc->out->key_a[sector] : dc->out->key_b[sector];
  preferred_keys_t *preferred = type == MF_KEY_A ? &dc->preferred_a : &dc->preferred_b;

  if (*found)
    return;
  memcpy(dst, key, NFC_MFC_KEY_SIZE);
  *found = true;
  if (type == MF_KEY_A)
    dc->out->keys_a_found++;
  else
    dc->out->keys_b_found++;
  preferred_add(preferred, key);

  const mf_key_cache_store_params_t cache = {
      .uid = dc->card->uid,
      .uid_len = dc->card->uid_len,
      .sector = sector,
      .sector_count = dc->out->sector_count,
      .type = type,
      .key = key,
  };
  mf_key_cache_store(&cache);
  ESP_LOGI(TAG, "Sector %02d Key %c verified", sector, type == MF_KEY_A ? 'A' : 'B');
}

static bool try_candidate(dump_ctx_t *dc,
                          int sector,
                          mf_key_type_t type,
                          const uint8_t key[NFC_MFC_KEY_SIZE],
                          int attempts) {
  uint8_t block = (uint8_t)nfc_mfc_dump_first_block(sector);
  if (auth_key(dc, block, type, key, attempts) != HB_NFC_OK)
    return false;
  remember_key(dc, sector, type, key);
  return true;
}

static bool find_key(dump_ctx_t *dc, int sector, mf_key_type_t type, bool exhaustive) {
  bool already = type == MF_KEY_A ? dc->out->key_a_found[sector] : dc->out->key_b_found[sector];
  if (already)
    return true;

  uint8_t key[NFC_MFC_KEY_SIZE] = {0};
  if (mf_key_cache_lookup(dc->card->uid, dc->card->uid_len, sector, type, key) &&
      try_candidate(dc, sector, type, key, AUTH_READ_ATTEMPTS)) {
    return true;
  }

  preferred_keys_t *preferred = type == MF_KEY_A ? &dc->preferred_a : &dc->preferred_b;
  for (int i = 0; i < preferred->count; i++) {
    if (try_candidate(dc, sector, type, preferred->data[i], AUTH_READ_ATTEMPTS))
      return true;
    if (cancelled(dc))
      return false;
  }

  int limit = exhaustive ? dc->dict_count : QUICK_DICT_KEYS;
  if (limit > dc->dict_count)
    limit = dc->dict_count;
  for (int i = 0; i < limit; i++) {
    mf_key_dict_get(i, key);
    if (preferred_contains(preferred, key))
      continue;
    if (try_candidate(dc, sector, type, key, AUTH_QUICK_ATTEMPTS))
      return true;
    if (cancelled(dc))
      return false;
  }
  return false;
}

static hb_nfc_err_t read_block_with_key(dump_ctx_t *dc,
                                        int sector,
                                        int block,
                                        mf_key_type_t type,
                                        const uint8_t key[NFC_MFC_KEY_SIZE]) {
  hb_nfc_err_t last = HB_NFC_ERR_AUTH;
  for (int attempt = 0; attempt < AUTH_READ_ATTEMPTS; attempt++) {
    last = auth_key(dc, (uint8_t)block, type, key, 1);
    if (last == HB_NFC_OK)
      last = mf_classic_read_block((uint8_t)block, dc->out->blocks[block]);
    if (last == HB_NFC_OK)
      return HB_NFC_OK;
    if (cancelled(dc))
      return ESP_ERR_INVALID_STATE;
    vTaskDelay(pdMS_TO_TICKS(AUTH_RETRY_DELAY_MS));
  }
  ESP_LOGW(TAG,
           "Sector %02d block %02d with Key%c failed: %s",
           sector,
           block,
           type == MF_KEY_A ? 'A' : 'B',
           hb_nfc_err_str(last));
  return last;
}

static bool trailer_key_b_readable(const uint8_t *trailer) {
  for (unsigned group = 0; group < MF_SMALL_BLOCK_COUNT; group++) {
    const unsigned c1 = (trailer[7] >> (4U + group)) & 1U;
    const unsigned c2 = (trailer[8] >> group) & 1U;
    const unsigned c3 = (trailer[8] >> (4U + group)) & 1U;
    if (c1 == ((trailer[6] >> group) & 1U) || c2 == ((trailer[6] >> (4U + group)) & 1U) ||
        c3 == ((trailer[7] >> group) & 1U))
      return false;
  }
  const unsigned access =
      ((trailer[7] >> 5U) & 4U) | ((trailer[8] >> 2U) & 2U) | ((trailer[8] >> 7U) & 1U);
  return access <= 2U;
}

static hb_nfc_err_t read_sector(dump_ctx_t *dc, int sector) {
  hb_nfc_err_t last = HB_NFC_ERR_AUTH;
  const int first = nfc_mfc_dump_first_block(sector);
  const int end = first + nfc_mfc_dump_sector_blocks(sector);

  for (int block = first; block < end; block++) {
    if (cancelled(dc))
      return ESP_ERR_INVALID_STATE;
    if (dc->out->block_read[block])
      continue;

    hb_nfc_err_t block_err = HB_NFC_ERR_AUTH;
    if (dc->out->key_a_found[sector])
      block_err = read_block_with_key(dc, sector, block, MF_KEY_A, dc->out->key_a[sector]);
    if (block_err != HB_NFC_OK && dc->out->key_b_found[sector])
      block_err = read_block_with_key(dc, sector, block, MF_KEY_B, dc->out->key_b[sector]);

    last = block_err;
    if (block_err == HB_NFC_OK) {
      dc->out->block_read[block] = true;
      dc->out->blocks_read++;
      if (block == end - 1 && !dc->out->key_b_found[sector] &&
          trailer_key_b_readable(dc->out->blocks[block]))
        remember_key(dc, sector, MF_KEY_B, dc->out->blocks[block] + KEY_B_OFFSET);
    }
    report_progress(dc, NFC_MFC_DUMP_PHASE_BLOCKS, sector);
  }

  for (int block = first; block < end; block++) {
    if (!dc->out->block_read[block])
      return last;
  }
  return HB_NFC_OK;
}

static void reconstruct_trailers(nfc_mfc_dump_result_t *dump) {
  for (int sector = 0; sector < dump->sector_count; sector++) {
    uint8_t *trailer =
        dump->blocks[nfc_mfc_dump_first_block(sector) + nfc_mfc_dump_sector_blocks(sector) - 1];
    if (dump->key_a_found[sector])
      memcpy(&trailer[0], dump->key_a[sector], NFC_MFC_KEY_SIZE);
    if (dump->key_b_found[sector])
      memcpy(&trailer[10], dump->key_b[sector], NFC_MFC_KEY_SIZE);
  }
}

hb_nfc_err_t nfc_mfc_dump_read(nfc_iso14443a_data_t *card,
                               const nfc_mfc_dump_callbacks_t *callbacks,
                               nfc_mfc_dump_result_t *out) {
  if (card == NULL || out == NULL || (card->uid_len != 4 && card->uid_len != 7) ||
      (card->sak != 0x09 && card->sak != 0x08 && card->sak != 0x18)) {
    return HB_NFC_ERR_PARAM;
  }

  memset(out, 0, sizeof(*out));
  out->err = HB_NFC_ERR_AUTH;
  out->sector_count = mf_classic_get_sector_count(mf_classic_get_type(card->sak));
  out->total_blocks = nfc_mfc_dump_first_block(out->sector_count);

  esp_err_t pm_err = power_manager_no_sleep_acquire();
  if (pm_err != ESP_OK) {
    ESP_LOGE(TAG, "failed to acquire NFC no-sleep lock: %s", esp_err_to_name(pm_err));
    return HB_NFC_ERR_INTERNAL;
  }

  mf_key_dict_init();
  mf_key_cache_init();

  dump_ctx_t dc = {
      .card = card,
      .out = out,
      .cancel_cb = callbacks == NULL ? NULL : callbacks->cancel,
      .progress_cb = callbacks == NULL ? NULL : callbacks->progress,
      .ctx = callbacks == NULL ? NULL : callbacks->context,
      .dict_count = mf_key_dict_count(),
  };

  if (dc.dict_count <= 0) {
    ESP_LOGE(TAG, "No MIFARE Classic dictionary available");
    (void)power_manager_no_sleep_release();
    return HB_NFC_ERR_NOT_FOUND;
  }
  if (nfc_poller_start() != HB_NFC_OK) {
    (void)power_manager_no_sleep_release();
    return HB_NFC_ERR_FIELD;
  }
  vTaskDelay(pdMS_TO_TICKS(RADIO_START_DELAY_MS));

  /* Fast pass: try cached/preferred/common keys, then obtain at least one
   * exhaustive key when needed so card data can be read without nested work. */
  for (int sector = 0; sector < out->sector_count; sector++) {
    if (cancelled(&dc)) {
      out->cancelled = true;
      out->err = ESP_ERR_INVALID_STATE;
      goto done;
    }
    report_progress(&dc, NFC_MFC_DUMP_PHASE_KEYS, sector);

    (void)find_key(&dc, sector, MF_KEY_A, false);
    (void)find_key(&dc, sector, MF_KEY_B, false);
    if (!out->key_a_found[sector] && !out->key_b_found[sector]) {
      (void)find_key(&dc, sector, MF_KEY_A, true);
      if (!out->key_a_found[sector])
        (void)find_key(&dc, sector, MF_KEY_B, true);
    }
    const int blocks_before = out->blocks_read;
    (void)read_sector(&dc, sector);
    /* A successful Key B authentication need not grant read access. Recover
     * the missing A key now instead of traversing the card without data. */
    if (out->blocks_read == blocks_before && !out->key_a_found[sector] &&
        out->key_b_found[sector] && !cancelled(&dc)) {
      report_progress(&dc, NFC_MFC_DUMP_PHASE_KEYS, sector);
      if (find_key(&dc, sector, MF_KEY_A, true))
        (void)read_sector(&dc, sector);
    }
  }

  /* Key completion pass. This remains dictionary-only and read-only. */
  for (int sector = 0; sector < out->sector_count; sector++) {
    if (cancelled(&dc)) {
      out->cancelled = true;
      out->err = ESP_ERR_INVALID_STATE;
      goto done;
    }
    report_progress(&dc, NFC_MFC_DUMP_PHASE_KEYS, sector);
    if (!out->key_a_found[sector])
      (void)find_key(&dc, sector, MF_KEY_A, true);
    if (!out->key_b_found[sector])
      (void)find_key(&dc, sector, MF_KEY_B, true);
  }

  /* RF recovery pass: after both sector keys are known, retry only the
   * missing blocks. Multiple bounded passes tolerate transient oscillator/TX
   * failures without ever rewriting data that was already verified. */
  for (int pass = 0; pass < RECOVERY_PASSES && out->blocks_read < out->total_blocks; pass++) {
    ESP_LOGI(
        TAG, "Recovery pass %d/3: %d/%d blocks", pass + 1, out->blocks_read, out->total_blocks);
    for (int sector = 0; sector < out->sector_count; sector++) {
      if (cancelled(&dc)) {
        out->cancelled = true;
        out->err = ESP_ERR_INVALID_STATE;
        goto done;
      }
      (void)read_sector(&dc, sector);
    }
  }

done:
  out->sectors_complete = 0;
  for (int sector = 0; sector < out->sector_count; sector++) {
    bool complete = true;
    for (int block = nfc_mfc_dump_first_block(sector);
         block < nfc_mfc_dump_first_block(sector) + nfc_mfc_dump_sector_blocks(sector);
         block++)
      complete &= out->block_read[block];
    if (complete)
      out->sectors_complete++;
  }

  reconstruct_trailers(out);
  mf_key_cache_save();
  out->emulation_ready = out->blocks_read == out->total_blocks &&
                         out->keys_a_found == out->sector_count &&
                         out->keys_b_found == out->sector_count;
  if (!out->cancelled)
    out->err = out->blocks_read == out->total_blocks ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;

  nfc_poller_stop();
  mf_classic_reset_auth();
  pm_err = power_manager_no_sleep_release();
  if (pm_err != ESP_OK)
    ESP_LOGW(TAG, "failed to release NFC no-sleep lock: %s", esp_err_to_name(pm_err));
  return out->err;
}

hb_nfc_err_t nfc_mfc_dump_read_1k(nfc_iso14443a_data_t *card,
                                  nfc_mfc_dump_cancel_cb_t cancel_cb,
                                  nfc_mfc_dump_progress_cb_t progress_cb,
                                  void *ctx,
                                  nfc_mfc_dump_result_t *out) {
  if (card == NULL || card->sak != 0x08)
    return HB_NFC_ERR_PARAM;
  const nfc_mfc_dump_callbacks_t callbacks = {cancel_cb, progress_cb, ctx};
  return nfc_mfc_dump_read(card, &callbacks, out);
}

static bool path_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static esp_err_t choose_paths(const nfc_iso14443a_data_t *card, nfc_mfc_dump_result_t *dump) {
  char uid[24] = {0};
  size_t pos = 0;
  for (uint8_t i = 0; i < card->uid_len; i++)
    pos += (size_t)snprintf(uid + pos, sizeof(uid) - pos, "%02X", card->uid[i]);
  for (int seq = 0; seq < 100; seq++) {
    char stem[NFC_MFC_PATH_MAX - 8];
    snprintf(stem, sizeof(stem), DUMP_DIR "/mf_%s_%02d", uid, seq);
    snprintf(dump->bin_path, sizeof(dump->bin_path), "%s.bin", stem);
    snprintf(dump->keys_path, sizeof(dump->keys_path), "%s.keys", stem);
    snprintf(dump->nfc_path, sizeof(dump->nfc_path), "%s.nfc", stem);
    if (!path_exists(dump->bin_path) && !path_exists(dump->keys_path) &&
        !path_exists(dump->nfc_path))
      return ESP_OK;
  }
  return ESP_ERR_NO_MEM;
}

/* Unknown data is never substituted with zero in a portable partial profile. */
static esp_err_t save_profile(const nfc_iso14443a_data_t *card, nfc_mfc_dump_result_t *dump) {
  int fd = open(dump->nfc_path, O_WRONLY | O_CREAT | O_EXCL, FILE_MODE);
  if (fd < 0)
    return ESP_FAIL;
  FILE *file = fdopen(fd, "w");
  if (file == NULL) {
    close(fd);
    remove(dump->nfc_path);
    return ESP_FAIL;
  }
  const char *type = dump->sector_count == 5 ? "MINI" : dump->sector_count == 16 ? "1K" : "4K";
  fprintf(file, "Filetype: Flipper NFC device\nVersion: 4\nDevice type: Mifare Classic\nUID:");
  for (uint8_t i = 0; i < card->uid_len; i++)
    fprintf(file, " %02X", card->uid[i]);
  fprintf(file,
          "\nATQA: %02X %02X\nSAK: %02X\nMifare Classic type: %s\nData format version: 2\n",
          card->atqa[1],
          card->atqa[0],
          card->sak,
          type);
  for (int block = 0; block < dump->total_blocks; block++) {
    const int sector = nfc_mfc_dump_block_sector(block);
    const bool trailer =
        block == nfc_mfc_dump_first_block(sector) + nfc_mfc_dump_sector_blocks(sector) - 1;
    fprintf(file, "Block %d:", block);
    for (int byte = 0; byte < NFC_MFC_BLOCK_SIZE; byte++) {
      bool known = dump->block_read[block];
      uint8_t value = dump->blocks[block][byte];
      if (trailer && byte < NFC_MFC_KEY_SIZE) {
        known = dump->key_a_found[sector];
        value = dump->key_a[sector][byte];
      } else if (trailer && byte >= KEY_B_OFFSET) {
        known = dump->key_b_found[sector];
        value = dump->key_b[sector][byte - KEY_B_OFFSET];
      }
      if (known)
        fprintf(file, " %02X", value);
      else
        fputs(" ??", file);
    }
    fputc('\n', file);
  }
  bool failed = ferror(file) != 0;
  if (fclose(file) != 0)
    failed = true;
  if (failed) {
    remove(dump->nfc_path);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t nfc_mfc_dump_save(const nfc_iso14443a_data_t *card, nfc_mfc_dump_result_t *dump) {
  if (card == NULL || dump == NULL || (card->uid_len != 4 && card->uid_len != 7) ||
      (dump->sector_count != 5 && dump->sector_count != 16 && dump->sector_count != 40) ||
      dump->total_blocks != nfc_mfc_dump_first_block(dump->sector_count) ||
      (dump->blocks_read == 0 && dump->keys_a_found == 0 && dump->keys_b_found == 0))
    return ESP_ERR_INVALID_STATE;
  dump->saved = false;
  if (storage_mkdir_recursive(DUMP_DIR) != ESP_OK || choose_paths(card, dump) != ESP_OK)
    return ESP_FAIL;
  esp_err_t error = save_profile(card, dump);
  if (error != ESP_OK)
    return error;
  /* Unknown blocks or keys deliberately have no .bin: raw images have no validity mask. */
  if (dump->emulation_ready) {
    int fd = open(dump->bin_path, O_WRONLY | O_CREAT | O_EXCL, FILE_MODE);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "wb");
    if (file == NULL) {
      if (fd >= 0) {
        close(fd);
        remove(dump->bin_path);
      }
      return ESP_FAIL;
    }
    size_t written = fwrite(dump->blocks, NFC_MFC_BLOCK_SIZE, dump->total_blocks, file);
    bool failed = written != (size_t)dump->total_blocks || ferror(file) != 0;
    if (fclose(file) != 0)
      failed = true;
    if (failed) {
      remove(dump->bin_path);
      return ESP_FAIL;
    }
  } else {
    dump->bin_path[0] = '\0';
  }
  dump->keys_path[0] = '\0'; /* Keys and their validity are in the .nfc profile. */
  dump->saved = true;
  ESP_LOGI(TAG, "Saved Classic profile: %d/%d blocks", dump->blocks_read, dump->total_blocks);
  return ESP_OK;
}
