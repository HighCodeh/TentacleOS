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
#include "nfc_listener.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "highboy_nfc.h"
#include "nfc_device.h"
#include "mf_classic.h"
#include "power_manager.h"
#include "rf_frontend.h"
#include "sys_prio.h"

#define NFC_LISTENER_STACK_BYTES 4096
#define NFC_LISTENER_RETRY_MS    10
#define NFC_LISTENER_WAIT_MS     3000

static const char *TAG = "NFC_LISTENER";

typedef enum {
  NFC_LISTENER_IDLE = 0,
  NFC_LISTENER_STARTING,
  NFC_LISTENER_RUNNING,
  NFC_LISTENER_STOPPING,
  NFC_LISTENER_COUNT
} nfc_listener_phase_t;

static TaskHandle_t s_task;
static _Atomic(nfc_listener_phase_t) s_phase;
static atomic_flag s_is_starting = ATOMIC_FLAG_INIT;
EXT_RAM_BSS_ATTR static mfc_emu_card_data_t s_card;
static bool s_has_power;
static bool s_has_reservation;
static bool s_has_emulator;
static bool s_is_plus;
static bool s_is_t2t;
static bool s_t2t_read_only;
static bool s_t2t_loaded;
static bool s_t2t_snapshot_valid;
static nfc_t2t_file_t s_t2t_card;
static t4t_emu_identity_t s_plus_identity;
static mfp_listener_t s_plus_listener;

static void listener_task(void *arg);
static hb_nfc_err_t start_card(void);
static hb_nfc_err_t configure_hardware(void);
static esp_err_t cleanup_hardware(void);

hb_nfc_err_t nfc_listener_start(const hb_nfc_card_data_t *card) {
  if (atomic_flag_test_and_set(&s_is_starting))
    return HB_NFC_ERR_INTERNAL;
  hb_nfc_err_t error = HB_NFC_ERR_INTERNAL;
  if (atomic_load(&s_phase) != NFC_LISTENER_IDLE)
    goto cleanup;
  if (card != NULL && card->protocol == HB_PROTO_MF_CLASSIC) {
    mfc_emu_card_data_init(&s_card, &card->iso14443a, mf_classic_get_type(card->iso14443a.sak));
  } else {
    int index = nfc_device_get_active();
    if (index < 0) {
      error = HB_NFC_ERR_PARAM;
      goto cleanup;
    }
    error = nfc_device_load(index, &s_card);
    if (error != HB_NFC_OK)
      goto cleanup;
  }
  s_is_t2t = false;
  s_is_plus = false;
  error = start_card();
cleanup:
  atomic_flag_clear(&s_is_starting);
  return error;
}

hb_nfc_err_t nfc_listener_start_emu(const mfc_emu_card_data_t *emu) {
  if (emu == NULL)
    return HB_NFC_ERR_PARAM;
  if (atomic_flag_test_and_set(&s_is_starting))
    return HB_NFC_ERR_INTERNAL;
  hb_nfc_err_t error = HB_NFC_ERR_INTERNAL;
  if (atomic_load(&s_phase) == NFC_LISTENER_IDLE) {
    s_is_t2t = false;
    s_is_plus = false;
    memcpy(&s_card, emu, sizeof(s_card));
    error = start_card();
  }
  atomic_flag_clear(&s_is_starting);
  return error;
}

hb_nfc_err_t nfc_listener_start_plus(const t4t_emu_identity_t *identity,
                                     const mfp_listener_config_t *config) {
  if (identity == NULL || config == NULL)
    return HB_NFC_ERR_PARAM;
  if (atomic_flag_test_and_set(&s_is_starting))
    return HB_NFC_ERR_INTERNAL;
  hb_nfc_err_t error = HB_NFC_ERR_INTERNAL;
  if (atomic_load(&s_phase) == NFC_LISTENER_IDLE) {
    error = mfp_listener_init(&s_plus_listener, config);
    if (error == HB_NFC_OK) {
      s_plus_identity = *identity;
      s_is_t2t = false;
      s_is_plus = true;
      error = start_card();
    }
  }
  atomic_flag_clear(&s_is_starting);
  return error;
}

static hb_nfc_err_t start_t2t(const nfc_t2t_file_t *card, bool is_read_only) {
  if (card == NULL)
    return HB_NFC_ERR_PARAM;
  if (atomic_flag_test_and_set(&s_is_starting))
    return HB_NFC_ERR_INTERNAL;
  hb_nfc_err_t error = HB_NFC_ERR_INTERNAL;
  if (atomic_load(&s_phase) == NFC_LISTENER_IDLE) {
    s_t2t_loaded = false;
    s_t2t_snapshot_valid = false;
    s_t2t_card = *card;
    s_t2t_read_only = is_read_only;
    s_is_t2t = true;
    s_is_plus = false;
    error = start_card();
  }
  atomic_flag_clear(&s_is_starting);
  return error;
}

hb_nfc_err_t nfc_listener_start_t2t(const nfc_t2t_file_t *card) {
  return start_t2t(card, true);
}

hb_nfc_err_t nfc_listener_start_t2t_mutable(const nfc_t2t_file_t *card) {
  return start_t2t(card, false);
}

esp_err_t nfc_listener_get_t2t_result(nfc_t2t_file_t *out_card) {
  if (out_card == NULL)
    return ESP_ERR_INVALID_ARG;
  if (atomic_flag_test_and_set(&s_is_starting))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = ESP_ERR_INVALID_STATE;
  if (atomic_load(&s_phase) == NFC_LISTENER_IDLE && s_is_t2t && s_t2t_snapshot_valid) {
    *out_card = s_t2t_card;
    error = ESP_OK;
  }
  atomic_flag_clear(&s_is_starting);
  return error;
}

void nfc_listener_stop(void) {
  nfc_listener_phase_t phase = atomic_load(&s_phase);
  while (phase != NFC_LISTENER_IDLE && phase != NFC_LISTENER_STOPPING &&
         !atomic_compare_exchange_weak(&s_phase, &phase, NFC_LISTENER_STOPPING)) {}
  TickType_t started = xTaskGetTickCount();
  while (atomic_load(&s_phase) != NFC_LISTENER_IDLE) {
    if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(NFC_LISTENER_WAIT_MS)) {
      ESP_LOGW(TAG, "Cleanup pending; antenna remains owned by firmware");
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(NFC_LISTENER_RETRY_MS));
  }
}

bool nfc_listener_is_active(void) {
  return atomic_load(&s_phase) != NFC_LISTENER_IDLE;
}

bool nfc_listener_is_running(void) {
  return atomic_load(&s_phase) == NFC_LISTENER_RUNNING;
}

static hb_nfc_err_t start_card(void) {
  if (s_task == NULL && xTaskCreatePinnedToCore(listener_task,
                                                "nfc_emu",
                                                NFC_LISTENER_STACK_BYTES,
                                                NULL,
                                                SYS_PRIO_SERVICE_HI,
                                                &s_task,
                                                SYS_CORE_RADIO) != pdPASS) {
    s_task = NULL;
    return HB_NFC_ERR_INTERNAL;
  }
  atomic_store(&s_phase, NFC_LISTENER_STARTING);
  xTaskNotifyGive(s_task);
  TickType_t started = xTaskGetTickCount();
  while (atomic_load(&s_phase) == NFC_LISTENER_STARTING) {
    if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(NFC_LISTENER_WAIT_MS)) {
      nfc_listener_phase_t expected = NFC_LISTENER_STARTING;
      atomic_compare_exchange_strong(&s_phase, &expected, NFC_LISTENER_STOPPING);
      return HB_NFC_ERR_INTERNAL;
    }
    vTaskDelay(pdMS_TO_TICKS(NFC_LISTENER_RETRY_MS));
  }
  return atomic_load(&s_phase) == NFC_LISTENER_RUNNING ? HB_NFC_OK : HB_NFC_ERR_INTERNAL;
}

static hb_nfc_err_t configure_hardware(void) {
  if (rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  s_has_reservation = true;
  if (power_manager_no_sleep_acquire() != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  s_has_power = true;
  highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
  if (highboy_nfc_init(&config) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (s_is_t2t) {
    hb_nfc_err_t error = t2t_emu_init(&s_t2t_card.config);
    if (error != HB_NFC_OK)
      return error;
    s_has_emulator = true;
    error = t2t_emu_load_memory(s_t2t_card.memory, s_t2t_card.length, s_t2t_read_only);
    if (error != HB_NFC_OK)
      return error;
    s_t2t_loaded = true;
    error = t2t_emu_configure_target();
    return error == HB_NFC_OK ? t2t_emu_start() : error;
  }
  if (s_is_plus) {
    hb_nfc_err_t error = t4t_emu_init_plus(&s_plus_identity, &s_plus_listener);
    if (error != HB_NFC_OK)
      return error;
    s_has_emulator = true;
    error = t4t_emu_configure_target();
    return error == HB_NFC_OK ? t4t_emu_start() : error;
  }
  hb_nfc_err_t error = mfc_emu_init(&s_card);
  if (error != HB_NFC_OK)
    return error;
  s_has_emulator = true;
  error = mfc_emu_configure_target();
  return error == HB_NFC_OK ? mfc_emu_start() : error;
}

static esp_err_t cleanup_hardware(void) {
  if (s_has_reservation) {
    if (s_has_emulator) {
      if (s_is_t2t) {
        if (s_t2t_loaded)
          s_t2t_snapshot_valid =
              t2t_emu_snapshot(&s_t2t_card.config, s_t2t_card.memory, s_t2t_card.length) ==
              HB_NFC_OK;
        s_t2t_loaded = false;
        t2t_emu_stop();
      } else if (s_is_plus)
        t4t_emu_stop();
      else
        mfc_emu_stop();
      s_has_emulator = false;
    }
    if (highboy_nfc_is_initialized()) {
      esp_err_t error = highboy_nfc_field_off();
      if (error != ESP_OK)
        return error;
      highboy_nfc_deinit();
      if (highboy_nfc_is_initialized())
        return ESP_FAIL;
    }
    esp_err_t error = rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ);
    if (error != ESP_OK)
      return error;
    s_has_reservation = false;
  }
  if (s_has_power) {
    esp_err_t error = power_manager_no_sleep_release();
    if (error != ESP_OK)
      return error;
    s_has_power = false;
  }
  return ESP_OK;
}

static void listener_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (atomic_load(&s_phase) == NFC_LISTENER_STARTING) {
      hb_nfc_err_t error = configure_hardware();
      nfc_listener_phase_t expected = NFC_LISTENER_STARTING;
      atomic_compare_exchange_strong(
          &s_phase, &expected, error == HB_NFC_OK ? NFC_LISTENER_RUNNING : NFC_LISTENER_STOPPING);
    }
    while (atomic_load(&s_phase) == NFC_LISTENER_RUNNING) {
      if (s_is_t2t) {
        t2t_emu_run_step();
        if (t2t_emu_has_error())
          atomic_store(&s_phase, NFC_LISTENER_STOPPING);
      } else if (s_is_plus) {
        t4t_emu_run_step();
        if (t4t_emu_has_error())
          atomic_store(&s_phase, NFC_LISTENER_STOPPING);
      } else if (mfc_emu_run_step() == MFC_EMU_STATE_ERROR)
        atomic_store(&s_phase, NFC_LISTENER_STOPPING);
      taskYIELD();
    }
    while (cleanup_hardware() != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(NFC_LISTENER_RETRY_MS));
    atomic_store(&s_phase, NFC_LISTENER_IDLE);
  }
}
