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

#include "nfc_mfkey.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mf_classic_emu.h"
#include "mf_classic.h"
#include "mf_key_dict.h"
#include "mf_key_cache.h"
#include "nfc_listener.h"
#include "power_manager.h"
#include "storage_mkdir.h"
#include "tos_storage_paths.h"

#define CAPTURE_SLOTS   (MFC_EMU_MAX_SECTORS * 2)
#define CAPTURE_POLL_MS 50
#define CAPTURE_PATH    TOS_PATH_NFC "/mfkey"
#define FILE_MODE       0600
#define KEY_BYTES       6
static const char *TAG = "NFC_MFKEY";
static atomic_flag s_running = ATOMIC_FLAG_INIT;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
typedef struct {
  mfc_emu_capture_t sample[2];
  unsigned count;
} nfc_mfkey_slot_t;
/* Static PSRAM remains valid even if hardware cleanup is delayed. Only this
 * worker owns the slots; listener writes under s_mux until it is stopped. */
EXT_RAM_BSS_ATTR static nfc_mfkey_slot_t s_slots[CAPTURE_SLOTS];
static nfc_mfkey_status_t s_status;

static void capture(const mfc_emu_capture_t *sample, void *context) {
  (void)context;
  if (sample->sector >= MFC_EMU_MAX_SECTORS ||
      (sample->key_type != MF_KEY_A && sample->key_type != MF_KEY_B))
    return;
  const unsigned index = sample->sector * 2U + (sample->key_type == MF_KEY_B);
  portENTER_CRITICAL(&s_mux);
  nfc_mfkey_slot_t *slot = &s_slots[index];
  if (slot->count < 2 && (slot->count == 0 || sample->cuid == slot->sample[0].cuid) &&
      (slot->count == 0 || sample->nt != slot->sample[0].nt || sample->nr != slot->sample[0].nr)) {
    slot->sample[slot->count++] = *sample;
    s_status.samples++;
    if (slot->count == 2)
      s_status.pairs++;
  }
  portEXIT_CRITICAL(&s_mux);
}

static void publish(const nfc_mfkey_callbacks_t *callbacks) {
  nfc_mfkey_status_t status;
  portENTER_CRITICAL(&s_mux);
  status = s_status;
  portEXIT_CRITICAL(&s_mux);
  if (callbacks->progress != NULL)
    callbacks->progress(&status, callbacks->context);
}

static bool recover_progress(uint32_t work, void *context) {
  (void)work;
  const nfc_mfkey_callbacks_t *callbacks = context;
  return callbacks->control(callbacks->context) != NFC_MFKEY_CANCEL;
}

/* Flipper-compatible offline log. Kept locally on SD, never printed to console. */
static bool save_captures(void) {
  if (s_status.pairs == 0 || storage_mkdir_recursive(CAPTURE_PATH) != ESP_OK)
    return false;
  char path[128];
  snprintf(path, sizeof(path), CAPTURE_PATH "/reader_%08lx.log", (unsigned long)esp_random());
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, FILE_MODE);
  if (fd < 0)
    return false;
  FILE *file = fdopen(fd, "w");
  if (file == NULL) {
    close(fd);
    remove(path);
    return false;
  }
  for (unsigned i = 0; i < CAPTURE_SLOTS; i++) {
    const nfc_mfkey_slot_t *slot = &s_slots[i];
    if (slot->count != 2)
      continue;
    const mfc_emu_capture_t *a = &slot->sample[0], *b = &slot->sample[1];
    fprintf(
        file,
        "Sec %u key %c cuid %08lx nt0 %08lx nr0 %08lx ar0 %08lx nt1 %08lx nr1 %08lx ar1 %08lx\n",
        a->sector,
        a->key_type == MF_KEY_A ? 'A' : 'B',
        (unsigned long)a->cuid,
        (unsigned long)a->nt,
        (unsigned long)a->nr,
        (unsigned long)a->ar,
        (unsigned long)b->nt,
        (unsigned long)b->nr,
        (unsigned long)b->ar);
  }
  bool failed = ferror(file) != 0;
  if (fclose(file) != 0)
    failed = true;
  if (failed)
    remove(path);
  return !failed;
}

esp_err_t nfc_mfkey_run(const nfc_iso14443a_data_t *identity,
                        const nfc_mfkey_callbacks_t *callbacks,
                        nfc_mfkey_status_t *out) {
  if (identity == NULL || callbacks == NULL || callbacks->control == NULL || out == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  if (atomic_flag_test_and_set(&s_running))
    return ESP_ERR_INVALID_STATE;
  if (nfc_listener_is_active()) {
    atomic_flag_clear(&s_running);
    return ESP_ERR_INVALID_STATE;
  }
  memset(s_slots, 0, sizeof(s_slots));
  memset(&s_status, 0, sizeof(s_status));
  bool power = power_manager_no_sleep_acquire() == ESP_OK;
  bool listener = false;
  s_status.error = power ? ESP_OK : ESP_ERR_INVALID_STATE;
  if (!power)
    goto done;
  s_status.error = nfc_listener_start_capture(identity, capture, NULL);
  listener = s_status.error == ESP_OK;
  if (!listener)
    goto stop;
  bool stopped_by_user = false;
  while (nfc_listener_is_running()) {
    publish(callbacks);
    nfc_mfkey_control_t control = callbacks->control(callbacks->context);
    if (control != NFC_MFKEY_CONTINUE) {
      stopped_by_user = true;
      s_status.cancelled = control == NFC_MFKEY_CANCEL;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(CAPTURE_POLL_MS));
  }
  if (!stopped_by_user)
    s_status.error = ESP_FAIL;
stop:
  if (listener || (s_status.error != ESP_ERR_INVALID_STATE && nfc_listener_is_active())) {
    nfc_listener_stop();
    while (nfc_listener_is_active())
      vTaskDelay(pdMS_TO_TICKS(CAPTURE_POLL_MS));
  }
  s_status.captures_saved = save_captures();
  if (s_status.cancelled || s_status.error != ESP_OK)
    goto done;
  s_status.phase = NFC_MFKEY_RECOVER;
  publish(callbacks);
  mf_key_dict_init();
  mf_key_cache_init();
  for (unsigned i = 0; i < CAPTURE_SLOTS; i++) {
    const nfc_mfkey_slot_t *slot = &s_slots[i];
    if (slot->count != 2)
      continue;
    const mfc_emu_capture_t *a = &slot->sample[0], *b = &slot->sample[1];
    mfkey_pair_t pair = {a->cuid, a->nt, a->nr, a->ar, b->nt, b->nr, b->ar};
    uint64_t key = 0;
    s_status.recovery = mfkey_recover(&pair, &key, recover_progress, (void *)callbacks);
    if (s_status.recovery == MFKEY_RESULT_CANCELLED) {
      s_status.cancelled = true;
      break;
    }
    s_status.processed++;
    if (s_status.recovery == MFKEY_RESULT_FOUND) {
      uint8_t bytes[KEY_BYTES];
      for (unsigned byte = 0; byte < KEY_BYTES; byte++)
        bytes[byte] = (uint8_t)(key >> ((KEY_BYTES - byte - 1) * 8U));
      s_status.recovered++;
      if (mf_key_dict_add(bytes) == HB_NFC_OK) {
        s_status.saved++;
      } else {
        s_status.error = ESP_FAIL;
      }
      /* Cache association is provisional until a fresh card authentication;
       * dump reader always verifies cached keys before marking them known. */
      const mf_key_cache_store_params_t entry = {
          .uid = identity->uid,
          .uid_len = identity->uid_len,
          .sector = a->sector,
          .sector_count = mf_classic_get_sector_count(mf_classic_get_type(identity->sak)),
          .type = a->key_type,
          .key = bytes,
      };
      mf_key_cache_store(&entry);
      memset(bytes, 0, sizeof(bytes));
      key = 0;
    } else if (s_status.recovery == MFKEY_RESULT_NO_MEMORY ||
               s_status.recovery == MFKEY_RESULT_CAPACITY) {
      s_status.error = ESP_ERR_NO_MEM;
      break;
    }
    publish(callbacks);
  }
  mf_key_cache_save();
done:
  s_status.phase = NFC_MFKEY_DONE;
  *out = s_status;
  publish(callbacks);
  if (power)
    (void)power_manager_no_sleep_release();
  memset(s_slots, 0, sizeof(s_slots));
  ESP_LOGI(TAG,
           "Reader capture finished: %u pairs, %u validated, %u saved",
           s_status.pairs,
           s_status.recovered,
           s_status.saved);
  atomic_flag_clear(&s_running);
  return s_status.error;
}
