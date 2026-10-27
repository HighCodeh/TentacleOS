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
    memcpy(&s_card, emu, sizeof(s_card));
    error = start_card();
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
      if (mfc_emu_run_step() == MFC_EMU_STATE_ERROR)
        atomic_store(&s_phase, NFC_LISTENER_STOPPING);
      taskYIELD();
    }
    while (cleanup_hardware() != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(NFC_LISTENER_RETRY_MS));
    atomic_store(&s_phase, NFC_LISTENER_IDLE);
  }
}
