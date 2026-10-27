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

#include "nfc_mfc_write.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_manager.h"

#include "mf_classic.h"
#include "mf_key_cache.h"
#include "mf_key_dict.h"
#include "nfc_mfc_dump.h"
#include "nfc_mfc_ndef.h"
#include "nfc_poller.h"
#include "poller.h"

static const char *TAG = "NFC_MFC_WRITE";

#define RADIO_START_DELAY_MS 200
#define WRITE_RETRY_COUNT    2
#define WRITE_RETRY_DELAY_MS 12
#define MFC_SAK_1K           0x08U
#define MFC_SAK_1K_INF       0x88U

typedef struct {
  uint8_t key[6];
  mf_key_type_t type;
  bool valid;
} target_key_t;

typedef struct {
  uint8_t source[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE];
  bool write_mask[NFC_MFC1K_BLOCKS];
  int blocks_expected;
  nfc_iso14443a_data_t target;
  nfc_mfc_write_cancel_cb_t cancel_cb;
  nfc_mfc_write_progress_cb_t progress_cb;
  void *cb_ctx;
  nfc_mfc_write_result_t *out;
  int dict_count;
} write_ctx_t;

static bool cancelled(const write_ctx_t *wc) {
  return wc->cancel_cb != NULL && wc->cancel_cb(wc->cb_ctx);
}

static void
report_progress(const write_ctx_t *wc, nfc_mfc_write_phase_t phase, int sector, int block) {
  if (wc->progress_cb == NULL)
    return;
  const nfc_mfc_write_progress_t progress = {
      .phase = phase,
      .sector = sector,
      .block = block,
      .blocks_written = wc->out->blocks_written,
      .blocks_verified = wc->out->blocks_verified,
      .sectors_unlocked = wc->out->sectors_unlocked,
  };
  wc->progress_cb(&progress, wc->cb_ctx);
}

static bool same_uid(const nfc_iso14443a_data_t *a, const nfc_iso14443a_data_t *b) {
  return a->uid_len == b->uid_len && memcmp(a->uid, b->uid, a->uid_len) == 0;
}

static hb_nfc_err_t
authenticate(write_ctx_t *wc, uint8_t block, mf_key_type_t type, const uint8_t key_bytes[6]) {
  nfc_iso14443a_data_t selected = wc->target;
  mf_classic_key_t key = {0};
  memcpy(key.data, key_bytes, sizeof(key.data));
  mf_classic_reset_auth();
  hb_nfc_err_t err = iso14443a_poller_reselect(&selected);
  if (err != HB_NFC_OK)
    return err;
  if (!same_uid(&selected, &wc->target))
    return HB_NFC_ERR_PROTOCOL;
  return mf_classic_auth(block, type, &key, wc->target.uid);
}

static bool try_key(write_ctx_t *wc,
                    int sector,
                    mf_key_type_t type,
                    const uint8_t candidate[6],
                    target_key_t *out) {
  if (authenticate(wc, (uint8_t)(sector * 4), type, candidate) != HB_NFC_OK)
    return false;
  memcpy(out->key, candidate, sizeof(out->key));
  out->type = type;
  out->valid = true;
  const mf_key_cache_store_params_t cache = {
      .uid = wc->target.uid,
      .uid_len = wc->target.uid_len,
      .sector = sector,
      .sector_count = NFC_MFC1K_SECTORS,
      .type = type,
      .key = out->key,
  };
  mf_key_cache_store(&cache);
  (void)mf_key_dict_add(out->key);
  return true;
}

static bool find_key(write_ctx_t *wc, int sector, mf_key_type_t type, target_key_t *out) {
  uint8_t candidate[6] = {0};
  const uint8_t *trailer = wc->source[sector * 4 + 3];
  const uint8_t *source_key = type == MF_KEY_A ? trailer : trailer + 10;

  if (try_key(wc, sector, type, source_key, out))
    return true;

  if (mf_key_cache_lookup(wc->target.uid, wc->target.uid_len, sector, type, candidate) &&
      memcmp(candidate, source_key, sizeof(candidate)) != 0 &&
      try_key(wc, sector, type, candidate, out)) {
    return true;
  }

  for (int i = 0; i < wc->dict_count && !cancelled(wc); i++) {
    mf_key_dict_get(i, candidate);
    if (memcmp(candidate, source_key, sizeof(candidate)) == 0)
      continue;
    if (try_key(wc, sector, type, candidate, out))
      return true;
  }
  return false;
}

static hb_nfc_err_t write_and_verify(write_ctx_t *wc, int sector, int block, target_key_t *key) {
  hb_nfc_err_t last = HB_NFC_ERR_AUTH;
  for (int attempt = 0; attempt < WRITE_RETRY_COUNT; attempt++) {
    if (cancelled(wc))
      return ESP_ERR_INVALID_STATE;
    if (attempt > 0)
      vTaskDelay(pdMS_TO_TICKS(WRITE_RETRY_DELAY_MS));

    last = authenticate(wc, (uint8_t)block, key->type, key->key);
    if (last != HB_NFC_OK)
      continue;
    report_progress(wc, NFC_MFC_WRITE_PHASE_WRITE, sector, block);
    last = mf_classic_write_block((uint8_t)block, wc->source[block]);
    if (last != HB_NFC_OK)
      continue;
    wc->out->blocks_written++;

    uint8_t verify[NFC_MFC_BLOCK_SIZE] = {0};
    last = authenticate(wc, (uint8_t)block, key->type, key->key);
    if (last != HB_NFC_OK)
      continue;
    report_progress(wc, NFC_MFC_WRITE_PHASE_VERIFY, sector, block);
    last = mf_classic_read_block((uint8_t)block, verify);
    if (last == HB_NFC_OK && memcmp(verify, wc->source[block], sizeof(verify)) == 0) {
      wc->out->blocks_verified++;
      return HB_NFC_OK;
    }
    if (last == HB_NFC_OK)
      last = HB_NFC_ERR_PROTOCOL;
  }
  return last;
}

static hb_nfc_err_t load_dump(const char *path,
                              uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE]) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL)
    return HB_NFC_ERR_NOT_FOUND;
  size_t count = fread(blocks, NFC_MFC_BLOCK_SIZE, NFC_MFC1K_BLOCKS, fp);
  int extra = fgetc(fp);
  fclose(fp);
  return count == NFC_MFC1K_BLOCKS && extra == EOF ? HB_NFC_OK : HB_NFC_ERR_PARAM;
}

static bool sector_has_writes(const write_ctx_t *wc, int sector) {
  for (int offset = 0; offset < 3; offset++) {
    const int block = sector * 4 + offset;
    if (block != 0 && wc->write_mask[block])
      return true;
  }
  return false;
}

static hb_nfc_err_t write_source_1k(write_ctx_t *wc) {
  if (wc == NULL || wc->out == NULL || wc->blocks_expected <= 0)
    return HB_NFC_ERR_PARAM;

  nfc_mfc_write_result_t *out = wc->out;
  out->blocks_expected = wc->blocks_expected;
  hb_nfc_err_t err = HB_NFC_ERR_INTERNAL;

  if (power_manager_no_sleep_acquire() != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  mf_key_dict_init();
  mf_key_cache_init();
  wc->dict_count = mf_key_dict_count();
  if (wc->dict_count <= 0) {
    out->err = HB_NFC_ERR_NOT_FOUND;
    goto done_no_radio;
  }

  err = nfc_poller_start();
  if (err != HB_NFC_OK) {
    out->err = err;
    goto done_no_radio;
  }
  vTaskDelay(pdMS_TO_TICKS(RADIO_START_DELAY_MS));

  err = iso14443a_poller_select(&wc->target);
  if (err != HB_NFC_OK) {
    out->err = HB_NFC_ERR_NOT_FOUND;
    goto done;
  }
  out->target = wc->target;
  if (wc->target.sak != MFC_SAK_1K && wc->target.sak != MFC_SAK_1K_INF) {
    out->err = HB_NFC_ERR_PROTOCOL;
    goto done;
  }

  for (int sector = 0; sector < NFC_MFC1K_SECTORS; sector++) {
    if (!sector_has_writes(wc, sector))
      continue;
    if (cancelled(wc)) {
      out->cancelled = true;
      out->err = ESP_ERR_INVALID_STATE;
      goto done;
    }

    report_progress(wc, NFC_MFC_WRITE_PHASE_FIND_KEY, sector, sector * 4);
    target_key_t key = {0};
    if (!find_key(wc, sector, MF_KEY_A, &key) && !find_key(wc, sector, MF_KEY_B, &key)) {
      if (out->failed_block < 0)
        out->failed_block = (sector == 0) ? 1 : sector * 4;
      continue;
    }
    out->sectors_unlocked++;

    for (int offset = 0; offset < 3; offset++) {
      int block = sector * 4 + offset;
      if (block == 0 || !wc->write_mask[block])
        continue;
      err = write_and_verify(wc, sector, block, &key);
      if (err == HB_NFC_OK)
        continue;

      /* Access bits may allow data writes only with Key B. Discover and retry
       * it lazily instead of doubling every dictionary pass. */
      if (key.type == MF_KEY_A) {
        target_key_t key_b = {0};
        if (find_key(wc, sector, MF_KEY_B, &key_b)) {
          key = key_b;
          err = write_and_verify(wc, sector, block, &key);
        }
      }
      if (err != HB_NFC_OK && out->failed_block < 0)
        out->failed_block = block;
    }
  }

  mf_key_cache_save();
  out->err = out->blocks_verified == wc->blocks_expected ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;

done:
  nfc_poller_stop();
  mf_classic_reset_auth();
done_no_radio:
  (void)power_manager_no_sleep_release();
  ESP_LOGI(TAG,
           "Write result: %d written, %d verified, %d sectors, err=%s",
           out->blocks_written,
           out->blocks_verified,
           out->sectors_unlocked,
           hb_nfc_err_str(out->err));
  return out->err;
}

static void init_write_ctx(write_ctx_t *wc,
                           nfc_mfc_write_cancel_cb_t cancel_cb,
                           nfc_mfc_write_progress_cb_t progress_cb,
                           void *ctx,
                           nfc_mfc_write_result_t *out) {
  memset(wc, 0, sizeof(*wc));
  memset(out, 0, sizeof(*out));
  out->failed_block = -1;
  out->err = HB_NFC_ERR_INTERNAL;
  wc->cancel_cb = cancel_cb;
  wc->progress_cb = progress_cb;
  wc->cb_ctx = ctx;
  wc->out = out;
}

hb_nfc_err_t nfc_mfc_write_data_1k(const char *dump_path,
                                   nfc_mfc_write_cancel_cb_t cancel_cb,
                                   nfc_mfc_write_progress_cb_t progress_cb,
                                   void *ctx,
                                   nfc_mfc_write_result_t *out) {
  if (dump_path == NULL || out == NULL)
    return HB_NFC_ERR_PARAM;

  write_ctx_t wc;
  init_write_ctx(&wc, cancel_cb, progress_cb, ctx, out);
  hb_nfc_err_t err = load_dump(dump_path, wc.source);
  if (err != HB_NFC_OK) {
    out->err = err;
    return err;
  }
  for (int sector = 0; sector < NFC_MFC1K_SECTORS; sector++) {
    for (int offset = 0; offset < 3; offset++) {
      const int block = sector * 4 + offset;
      if (block != 0)
        wc.write_mask[block] = true;
    }
  }
  wc.blocks_expected = NFC_MFC_WRITE_DATA_BLOCKS;
  return write_source_1k(&wc);
}

hb_nfc_err_t nfc_mfc_write_text_1k(const char *text,
                                   nfc_mfc_write_cancel_cb_t cancel_cb,
                                   nfc_mfc_write_progress_cb_t progress_cb,
                                   void *ctx,
                                   nfc_mfc_write_result_t *out) {
  if (text == NULL || out == NULL)
    return HB_NFC_ERR_PARAM;

  write_ctx_t wc;
  init_write_ctx(&wc, cancel_cb, progress_cb, ctx, out);
  size_t ndef_len = 0;
  hb_nfc_err_t err =
      nfc_mfc_ndef_build_text(text, wc.source, wc.write_mask, &wc.blocks_expected, &ndef_len);
  if (err != HB_NFC_OK) {
    out->err = err;
    return err;
  }
  ESP_LOGI(TAG,
           "NDEF text image: %u message bytes, %d writable blocks",
           (unsigned)ndef_len,
           wc.blocks_expected);
  return write_source_1k(&wc);
}
