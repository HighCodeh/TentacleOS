// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tos_api_nfc_session.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host_link_local_rf.h"
#include "mf_classic_emu.h"
#include "highboy_nfc.h"
#include "power_manager.h"
#include "rf_frontend.h"
#include "sys_prio.h"
#include "tos_app_ctx.h"

#define NFC_SESSION_UID_BYTES   4
#define NFC_SESSION_STACK_BYTES 4096
#define NFC_SESSION_WAIT_MS     3000
#define NFC_SESSION_RETRY_MS    10
#define NFC_SESSION_ATQA        0x04
#define NFC_SESSION_SAK         0x08

static const char *TAG = "TOS_NFC_SESSION";

typedef enum {
  TOS_NFC_SESSION_IDLE = 0,
  TOS_NFC_SESSION_STARTING,
  TOS_NFC_SESSION_RUNNING,
  TOS_NFC_SESSION_STOPPING,
  TOS_NFC_SESSION_COUNT
} tos_api_nfc_session_phase_t;

typedef struct {
  void *owner;
  uint32_t id;
  uint8_t uid[NFC_SESSION_UID_BYTES];
  tos_api_nfc_session_phase_t phase;
  esp_err_t error;
} tos_api_nfc_session_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static tos_api_nfc_session_t s_session;
static TaskHandle_t s_task;
static uint32_t s_sequence;
static bool s_has_emulator;
static bool s_has_reservation;
static bool s_has_power;

static void session_task(void *arg);
static esp_err_t start_hardware(void);
static esp_err_t stop_hardware(void);
static esp_err_t wait_session(uint32_t session, bool is_starting);

esp_err_t tos_api_nfc_session_start(const uint8_t *uid, uint8_t uid_len, uint32_t *out_session) {
  if (out_session == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_session = 0;
  if (uid == NULL || uid_len != NFC_SESSION_UID_BYTES)
    return ESP_ERR_INVALID_ARG;
  if (!tos_app_cap_check(TOS_CAP_RADIO_TX))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop() || !host_local_rf_is_ready())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(NFC_SESSION_WAIT_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t error = ESP_ERR_INVALID_STATE;
  uint32_t id = 0;
  portENTER_CRITICAL(&s_lock);
  bool can_start = s_session.phase == TOS_NFC_SESSION_IDLE && s_sequence != UINT32_MAX;
  portEXIT_CRITICAL(&s_lock);
  if (!can_start || tos_api_get()->should_stop())
    goto cleanup;
  if (s_task == NULL && xTaskCreatePinnedToCore(session_task,
                                                "nfc_app",
                                                NFC_SESSION_STACK_BYTES,
                                                NULL,
                                                SYS_PRIO_SERVICE_HI,
                                                &s_task,
                                                SYS_CORE_RADIO) != pdPASS) {
    s_task = NULL;
    ESP_LOGE(TAG, "Cannot create NFC session worker");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  portENTER_CRITICAL(&s_lock);
  id = ++s_sequence;
  s_session = (tos_api_nfc_session_t){
      .owner = xTaskGetCurrentTaskHandle(), .id = id, .phase = TOS_NFC_SESSION_STARTING};
  memcpy(s_session.uid, uid, sizeof(s_session.uid));
  portEXIT_CRITICAL(&s_lock);
  xTaskNotifyGive(s_task);
  error = ESP_OK;
cleanup:
  host_local_rf_unlock();
  if (error != ESP_OK)
    return error;
  error = wait_session(id, true);
  if (error == ESP_OK) {
    *out_session = id;
    tos_app_ctx_clear_resource_lost();
  }
  return error;
}

esp_err_t tos_api_nfc_session_stop(uint32_t session) {
  if (!tos_app_cap_check(TOS_CAP_RADIO_TX))
    return ESP_ERR_NOT_SUPPORTED;
  portENTER_CRITICAL(&s_lock);
  bool is_owner =
      session != 0 && session == s_session.id && s_session.owner == xTaskGetCurrentTaskHandle();
  if (is_owner && s_session.phase != TOS_NFC_SESSION_IDLE)
    s_session.phase = TOS_NFC_SESSION_STOPPING;
  portEXIT_CRITICAL(&s_lock);
  if (!is_owner)
    return ESP_ERR_INVALID_STATE;
  return wait_session(session, false);
}

void tos_api_nfc_session_cancel_owner(void *task) {
  portENTER_CRITICAL(&s_lock);
  if (task != NULL && s_session.owner == task) {
    s_session.owner = NULL;
    if (s_session.phase != TOS_NFC_SESSION_IDLE)
      s_session.phase = TOS_NFC_SESSION_STOPPING;
  }
  portEXIT_CRITICAL(&s_lock);
}

static esp_err_t wait_session(uint32_t session, bool is_starting) {
  TickType_t started = xTaskGetTickCount();
  for (;;) {
    bool should_stop = is_starting && tos_api_get()->should_stop();
    portENTER_CRITICAL(&s_lock);
    bool is_owner = session == s_session.id && s_session.owner == xTaskGetCurrentTaskHandle();
    tos_api_nfc_session_phase_t phase = s_session.phase;
    esp_err_t error = s_session.error;
    bool has_timeout = xTaskGetTickCount() - started >= pdMS_TO_TICKS(NFC_SESSION_WAIT_MS);
    if (is_owner && is_starting && (has_timeout || should_stop) && phase != TOS_NFC_SESSION_IDLE)
      s_session.phase = TOS_NFC_SESSION_STOPPING;
    portEXIT_CRITICAL(&s_lock);
    if (!is_owner)
      return ESP_ERR_INVALID_STATE;
    if (phase == TOS_NFC_SESSION_IDLE)
      return is_starting ? (error == ESP_OK ? ESP_ERR_INVALID_STATE : error) : ESP_OK;
    if (is_starting && phase == TOS_NFC_SESSION_RUNNING && !has_timeout)
      return ESP_OK;
    if (has_timeout)
      return ESP_ERR_TIMEOUT;
    vTaskDelay(pdMS_TO_TICKS(NFC_SESSION_RETRY_MS));
  }
}

static esp_err_t start_hardware(void) {
  esp_err_t error = rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ);
  if (error != ESP_OK)
    return error;
  s_has_reservation = true;
  error = power_manager_no_sleep_acquire();
  if (error != ESP_OK)
    return error;
  s_has_power = true;
  highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
  error = highboy_nfc_init(&config);
  if (error != ESP_OK)
    return error;
  mfc_emu_card_data_t *card = calloc(1, sizeof(*card));
  if (card == NULL) {
    ESP_LOGE(TAG, "Cannot allocate emulation card");
    return ESP_ERR_NO_MEM;
  }
  nfc_iso14443a_data_t identity = {
      .uid_len = NFC_SESSION_UID_BYTES, .atqa = {NFC_SESSION_ATQA, 0}, .sak = NFC_SESSION_SAK};
  memcpy(identity.uid, s_session.uid, sizeof(s_session.uid));
  mfc_emu_card_data_init(card, &identity, MF_CLASSIC_1K);
  hb_nfc_err_t nfc_error = mfc_emu_init(card);
  free(card);
  if (nfc_error != HB_NFC_OK)
    return ESP_FAIL;
  s_has_emulator = true;
  if (mfc_emu_configure_target() != HB_NFC_OK || mfc_emu_start() != HB_NFC_OK)
    return ESP_FAIL;
  return highboy_nfc_ping(NULL);
}

static esp_err_t stop_hardware(void) {
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

static void session_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      if (!host_local_rf_lock(NFC_SESSION_RETRY_MS)) {
        vTaskDelay(pdMS_TO_TICKS(NFC_SESSION_RETRY_MS));
        continue;
      }
      portENTER_CRITICAL(&s_lock);
      tos_api_nfc_session_phase_t phase = s_session.phase;
      portEXIT_CRITICAL(&s_lock);
      esp_err_t error = ESP_OK;
      if (phase == TOS_NFC_SESSION_STARTING)
        error = start_hardware();
      else if (phase == TOS_NFC_SESSION_RUNNING && mfc_emu_run_step() == MFC_EMU_STATE_ERROR)
        error = ESP_FAIL;
      portENTER_CRITICAL(&s_lock);
      if (error != ESP_OK) {
        s_session.error = error;
        s_session.phase = TOS_NFC_SESSION_STOPPING;
        tos_app_ctx_signal_resource_lost(s_session.owner);
      } else if (s_session.phase == TOS_NFC_SESSION_STARTING) {
        s_session.phase = TOS_NFC_SESSION_RUNNING;
      }
      bool is_stopping = s_session.phase == TOS_NFC_SESSION_STOPPING;
      portEXIT_CRITICAL(&s_lock);
      if (is_stopping) {
        error = stop_hardware();
        portENTER_CRITICAL(&s_lock);
        if (error == ESP_OK)
          s_session.phase = TOS_NFC_SESSION_IDLE;
        else
          s_session.error = error;
        portEXIT_CRITICAL(&s_lock);
      }
      portENTER_CRITICAL(&s_lock);
      bool is_idle = s_session.phase == TOS_NFC_SESSION_IDLE;
      portEXIT_CRITICAL(&s_lock);
      host_local_rf_unlock();
      if (is_idle)
        break;
      if (is_stopping)
        vTaskDelay(pdMS_TO_TICKS(NFC_SESSION_RETRY_MS));
      else
        taskYIELD();
    }
  }
}
