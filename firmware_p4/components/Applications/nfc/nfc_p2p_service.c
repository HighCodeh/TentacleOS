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

#include "nfc_p2p_service.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "highboy_nfc.h"
#include "llcp.h"
#include "ndef.h"
#include "nfc_poller.h"
#include "power_manager.h"
#include "snep.h"
#include "sys_prio.h"

#define P2P_TASK_STACK      6144
#define P2P_SEARCH_ATTEMPTS 120
#define P2P_SEARCH_DELAY_MS 100
#define P2P_RADIO_SETTLE_MS 100
#define P2P_SNEP_TIMEOUT_MS 1000
#define P2P_NDEF_MAX        256

static const char *TAG = "NFC_P2P";

static TaskHandle_t s_task;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static nfc_p2p_status_t s_status;
static bool s_cancel;

static void set_state(nfc_p2p_state_t state) {
  portENTER_CRITICAL(&s_mux);
  s_status.state = state;
  portEXIT_CRITICAL(&s_mux);
}

static bool cancel_requested(void) {
  bool cancel;
  portENTER_CRITICAL(&s_mux);
  cancel = s_cancel;
  portEXIT_CRITICAL(&s_mux);
  return cancel;
}

static void finish(nfc_p2p_state_t state, hb_nfc_err_t error) {
  portENTER_CRITICAL(&s_mux);
  s_status.state = state;
  s_status.error = error;
  s_status.running = false;
  s_task = NULL;
  portEXIT_CRITICAL(&s_mux);
}

static hb_nfc_err_t
build_payload(nfc_p2p_payload_t payload, uint8_t *out, size_t max, size_t *out_len) {
  if (out == NULL || out_len == NULL)
    return HB_NFC_ERR_PARAM;
  ndef_builder_t builder;
  ndef_builder_init(&builder, out, max);
  hb_nfc_err_t err;
  if (payload == NFC_P2P_PAYLOAD_URI)
    err = ndef_builder_add_uri(&builder, "https://highboy.local/");
  else
    err = ndef_builder_add_text(&builder, "High Boy NFC P2P", "en", false);
  *out_len = ndef_builder_len(&builder);
  return err;
}

static void p2p_task(void *arg) {
  const nfc_p2p_payload_t payload = (nfc_p2p_payload_t)(uintptr_t)arg;
  bool no_sleep_held = false;
  bool poller_started = false;
  hb_nfc_err_t err = HB_NFC_ERR_INTERNAL;

  set_state(NFC_P2P_STATE_INITIALIZING);
  if (!highboy_nfc_is_initialized()) {
    highboy_nfc_config_t cfg = HIGHBOY_NFC_CONFIG_DEFAULT();
    if (highboy_nfc_init(&cfg) != ESP_OK)
      goto done;
  }
  if (power_manager_no_sleep_acquire() != ESP_OK)
    goto done;
  no_sleep_held = true;
  err = nfc_poller_start();
  if (err != HB_NFC_OK)
    goto done;
  poller_started = true;
  vTaskDelay(pdMS_TO_TICKS(P2P_RADIO_SETTLE_MS));

  uint8_t ndef[P2P_NDEF_MAX];
  size_t ndef_len = 0;
  err = build_payload(payload, ndef, sizeof(ndef), &ndef_len);
  if (err != HB_NFC_OK || ndef_len == 0)
    goto done;
  portENTER_CRITICAL(&s_mux);
  s_status.ndef_len = (uint16_t)ndef_len;
  portEXIT_CRITICAL(&s_mux);

  llcp_link_t link;
  llcp_link_init(&link);
  set_state(NFC_P2P_STATE_SEARCHING);
  err = HB_NFC_ERR_NO_CARD;
  for (uint16_t attempt = 1; attempt <= P2P_SEARCH_ATTEMPTS; attempt++) {
    if (cancel_requested())
      break;
    portENTER_CRITICAL(&s_mux);
    s_status.attempts = attempt;
    portEXIT_CRITICAL(&s_mux);
    err = llcp_initiator_activate(&link);
    if (err == HB_NFC_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(P2P_SEARCH_DELAY_MS));
  }
  if (cancel_requested())
    goto done;
  if (err != HB_NFC_OK)
    goto done;

  portENTER_CRITICAL(&s_mux);
  s_status.state = NFC_P2P_STATE_LLCP_CONNECTED;
  s_status.negotiated_miu = link.negotiated.miu;
  portEXIT_CRITICAL(&s_mux);

  set_state(NFC_P2P_STATE_SNEP_SENDING);
  uint8_t response = 0;
  err = snep_client_put(&link, ndef, ndef_len, &response, P2P_SNEP_TIMEOUT_MS);
  portENTER_CRITICAL(&s_mux);
  s_status.snep_response = response;
  portEXIT_CRITICAL(&s_mux);

done:
  if (poller_started)
    nfc_poller_stop();
  else if (highboy_nfc_is_initialized())
    (void)highboy_nfc_field_off();
  if (no_sleep_held)
    (void)power_manager_no_sleep_release();

  if (cancel_requested()) {
    ESP_LOGI(TAG, "P2P transfer cancelled");
    finish(NFC_P2P_STATE_CANCELLED, ESP_ERR_INVALID_STATE);
  } else if (err == HB_NFC_OK) {
    ESP_LOGI(TAG, "SNEP PUT complete (%u NDEF bytes)", (unsigned)ndef_len);
    finish(NFC_P2P_STATE_DONE, HB_NFC_OK);
  } else {
    ESP_LOGW(TAG, "P2P stopped: %s", hb_nfc_err_str(err));
    finish(NFC_P2P_STATE_ERROR, err);
  }
  vTaskDelete(NULL);
}

hb_nfc_err_t nfc_p2p_service_start(nfc_p2p_payload_t payload) {
  if ((int)payload < 0 || payload >= NFC_P2P_PAYLOAD_COUNT)
    return HB_NFC_ERR_PARAM;

  portENTER_CRITICAL(&s_mux);
  if (s_task != NULL || s_status.running) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  memset(&s_status, 0, sizeof(s_status));
  s_status.state = NFC_P2P_STATE_INITIALIZING;
  s_status.payload = payload;
  s_status.running = true;
  s_cancel = false;
  portEXIT_CRITICAL(&s_mux);

  TaskHandle_t task = NULL;
  BaseType_t rc = xTaskCreatePinnedToCore(p2p_task,
                                          "nfc_p2p",
                                          P2P_TASK_STACK,
                                          (void *)(uintptr_t)payload,
                                          SYS_PRIO_SERVICE_HI,
                                          &task,
                                          SYS_CORE_RADIO);
  if (rc != pdPASS) {
    finish(NFC_P2P_STATE_ERROR, HB_NFC_ERR_INTERNAL);
    return HB_NFC_ERR_INTERNAL;
  }
  portENTER_CRITICAL(&s_mux);
  s_task = task;
  portEXIT_CRITICAL(&s_mux);
  return HB_NFC_OK;
}

void nfc_p2p_service_cancel(void) {
  portENTER_CRITICAL(&s_mux);
  s_cancel = true;
  portEXIT_CRITICAL(&s_mux);
}

void nfc_p2p_service_stop(void) {
  nfc_p2p_service_cancel();
  for (;;) {
    portENTER_CRITICAL(&s_mux);
    const bool running = s_task != NULL;
    portEXIT_CRITICAL(&s_mux);
    if (!running)
      break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void nfc_p2p_service_get_status(nfc_p2p_status_t *out) {
  if (out == NULL)
    return;
  portENTER_CRITICAL(&s_mux);
  *out = s_status;
  portEXIT_CRITICAL(&s_mux);
}

const char *nfc_p2p_payload_name(nfc_p2p_payload_t payload) {
  return payload == NFC_P2P_PAYLOAD_URI ? "URI: highboy.local" : "Text: High Boy NFC P2P";
}
