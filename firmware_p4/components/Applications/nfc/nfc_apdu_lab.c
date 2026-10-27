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

#include "nfc_apdu_lab.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "highboy_nfc.h"
#include "iso14443b.h"
#include "iso_dep.h"
#include "nfc_poller.h"
#include "poller.h"
#include "power_manager.h"
#include "sd_card_dir.h"
#include "sd_card_init.h"
#include "sd_card_write.h"
#include "sys_prio.h"

#define APDU_LAB_TASK_STACK        12288
#define APDU_LAB_QUEUE_DEPTH       3
#define APDU_LAB_TIMEOUT_MS        1000
#define APDU_LAB_A_SCAN_ATTEMPTS   20
#define APDU_LAB_A_SCAN_DELAY_MS   50
#define APDU_LAB_FIELD_SETTLE_MS   120
#define APDU_LAB_RF_REGULATOR_MV   3300U
#define APDU_LAB_RECOVERY_DELAY_MS 12

typedef struct {
  uint8_t command[NFC_APDU_LAB_TX_MAX];
  size_t command_len;
  nfc_apdu_risk_t risk;
  bool stop;
} apdu_lab_request_t;

static const char *TAG = "NFC_APDU_LAB";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static nfc_apdu_lab_status_t s_status;
// APDU history is diagnostic data, not a DMA/ISR buffer. It is large enough
// (~12 KB) to materially fragment internal RAM, so retain it in PSRAM.
EXT_RAM_BSS_ATTR static nfc_apdu_lab_entry_t s_history[NFC_APDU_LAB_HISTORY_MAX];
static uint8_t s_history_next;
static uint32_t s_sequence;
static QueueHandle_t s_queue;
static TaskHandle_t s_task;

static void set_state(nfc_apdu_lab_state_t state, hb_nfc_err_t error) {
  portENTER_CRITICAL(&s_mux);
  s_status.state = state;
  s_status.error = error;
  portEXIT_CRITICAL(&s_mux);
}

static void publish_link_a(const nfc_iso14443a_data_t *card, const nfc_iso_dep_data_t *dep) {
  portENTER_CRITICAL(&s_mux);
  s_status.protocol = HB_PROTO_ISO14443_4A;
  s_status.identifier_len =
      card->uid_len < sizeof(s_status.identifier) ? card->uid_len : sizeof(s_status.identifier);
  memcpy(s_status.identifier, card->uid, s_status.identifier_len);
  s_status.ats_len = dep->ats_len < sizeof(s_status.ats) ? dep->ats_len : sizeof(s_status.ats);
  memcpy(s_status.ats, dep->ats, s_status.ats_len);
  s_status.frame_size = dep->fsc;
  s_status.fwi = dep->fwi;
  portEXIT_CRITICAL(&s_mux);
}

static void publish_link_b(const nfc_iso14443b_data_t *card) {
  portENTER_CRITICAL(&s_mux);
  s_status.protocol = HB_PROTO_ISO14443_4B;
  s_status.identifier_len = sizeof(card->pupi);
  memcpy(s_status.identifier, card->pupi, sizeof(card->pupi));
  s_status.ats_len = 0;
  s_status.frame_size = 0;
  s_status.fwi = 0;
  portEXIT_CRITICAL(&s_mux);
}

static void publish_rf_diagnostics(void) {
  uint16_t regulator_mv = 0;
  bool current_limit = false;
  bool manual = false;
  const esp_err_t err = highboy_nfc_get_regulator(&regulator_mv, &current_limit, &manual, NULL);
  const uint8_t amplitude = highboy_nfc_measure_amplitude();
  portENTER_CRITICAL(&s_mux);
  s_status.regulator_mv = err == ESP_OK ? regulator_mv : 0U;
  s_status.regulator_current_limit = err == ESP_OK && current_limit;
  s_status.regulator_manual = err == ESP_OK && manual;
  s_status.rf_amplitude = amplitude;
  portEXIT_CRITICAL(&s_mux);
}

static void trace_entry(const nfc_apdu_lab_entry_t *entry, hb_nfc_protocol_t protocol) {
  if (!sd_is_mounted())
    return;
  if (sd_dir_create("/nfc") != ESP_OK)
    return;

  char tx_hex[NFC_APDU_LAB_TX_MAX * 2U + 1U];
  char rx_hex[NFC_APDU_LAB_RX_MAX * 2U + 1U];
  char line[2200];
  nfc_apdu_hex_encode(entry->tx, entry->tx_len, '\0', tx_hex, sizeof(tx_hex));
  nfc_apdu_hex_encode(entry->rx, entry->rx_len, '\0', rx_hex, sizeof(rx_hex));
  snprintf(
      line,
      sizeof(line),
      "seq=%lu uptime_us=%lld proto=%s risk=%s tx=%s rx=%s sw=%04X time_us=%lu exchanges=%u%s%s%s",
      (unsigned long)entry->sequence,
      (long long)esp_timer_get_time(),
      protocol == HB_PROTO_ISO14443_4B ? "ISO14443-4B" : "ISO14443-4A",
      nfc_apdu_risk_name(entry->risk),
      tx_hex,
      rx_hex,
      entry->sw,
      (unsigned long)entry->elapsed_us,
      entry->exchanges,
      entry->le_corrected ? " le-corrected" : "",
      entry->truncated ? " truncated" : "",
      entry->session_recovered ? " session-recovered" : "");
  if (sd_append_line(NFC_APDU_LAB_TRACE_PATH, line) == ESP_OK) {
    portENTER_CRITICAL(&s_mux);
    s_status.trace_enabled = true;
    portEXIT_CRITICAL(&s_mux);
  }
}

static void publish_entry(nfc_apdu_lab_entry_t *entry, hb_nfc_protocol_t protocol) {
  portENTER_CRITICAL(&s_mux);
  entry->sequence = ++s_sequence;
  s_history[s_history_next] = *entry;
  s_history_next = (uint8_t)((s_history_next + 1U) % NFC_APDU_LAB_HISTORY_MAX);
  if (s_status.history_count < NFC_APDU_LAB_HISTORY_MAX)
    s_status.history_count++;
  s_status.latest_sequence = entry->sequence;
  portEXIT_CRITICAL(&s_mux);
  trace_entry(entry, protocol);
}

static hb_nfc_err_t activate_type_a(nfc_iso14443a_data_t *card, nfc_iso_dep_data_t *dep) {
  for (int attempt = 0; attempt < APDU_LAB_A_SCAN_ATTEMPTS; attempt++) {
    hb_nfc_err_t err = iso14443a_poller_select(card);
    if (err == HB_NFC_OK) {
      if ((card->sak & 0x20U) == 0U)
        return HB_NFC_ERR_PROTOCOL;
      return iso_dep_rats(8, 0, dep);
    }
    vTaskDelay(pdMS_TO_TICKS(APDU_LAB_A_SCAN_DELAY_MS));
  }
  return HB_NFC_ERR_NO_CARD;
}

static hb_nfc_err_t recover_session(nfc_apdu_channel_t *channel,
                                    nfc_iso14443a_data_t *card_a,
                                    nfc_iso_dep_data_t *dep_a,
                                    nfc_iso14443b_data_t *card_b) {
  hb_nfc_err_t err;
  if (channel->proto == HB_PROTO_ISO14443_4A) {
    err = iso14443a_poller_reselect(card_a);
    if (err == HB_NFC_OK)
      err = iso_dep_rats(8, 0, dep_a);
    if (err == HB_NFC_OK)
      publish_link_a(card_a, dep_a);
    return err;
  }

  if (channel->proto == HB_PROTO_ISO14443_4B) {
    nfc_poller_stop();
    vTaskDelay(pdMS_TO_TICKS(APDU_LAB_RECOVERY_DELAY_MS));
    err = iso14443b_poller_init();
    if (err == HB_NFC_OK)
      err = iso14443b_select(card_b, 0x00U, 0x00U);
    if (err == HB_NFC_OK)
      publish_link_b(card_b);
    return err;
  }
  return HB_NFC_ERR_PROTOCOL;
}

static hb_nfc_err_t switch_rf_to_auto(void) {
  nfc_poller_stop();
  if (highboy_nfc_adjust_regulator_auto() != ESP_OK)
    return HB_NFC_ERR_FIELD;
  if (nfc_poller_start() != HB_NFC_OK)
    return HB_NFC_ERR_FIELD;
  vTaskDelay(pdMS_TO_TICKS(APDU_LAB_FIELD_SETTLE_MS));
  publish_rf_diagnostics();
  return HB_NFC_OK;
}

static void apdu_lab_task(void *arg) {
  (void)arg;
  bool no_sleep = false;
  bool field = false;
  nfc_iso14443a_data_t card_a = {0};
  nfc_iso_dep_data_t dep_a = {0};
  nfc_iso14443b_data_t card_b = {0};
  nfc_apdu_channel_t channel = {0};

  set_state(NFC_APDU_LAB_STARTING, HB_NFC_OK);
  if (!highboy_nfc_is_initialized()) {
    highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
    if (highboy_nfc_init(&config) != ESP_OK) {
      set_state(NFC_APDU_LAB_ERROR, HB_NFC_ERR_INTERNAL);
      goto done;
    }
  }
  if (power_manager_no_sleep_acquire() != ESP_OK) {
    set_state(NFC_APDU_LAB_ERROR, HB_NFC_ERR_INTERNAL);
    goto done;
  }
  no_sleep = true;
  /* High Boy supplies the ST25R3916 from 3.3 V.  Select the matching manual
   * driver-regulator target before the RF field is enabled. */
  if (highboy_nfc_set_regulator_mv(APDU_LAB_RF_REGULATOR_MV) != ESP_OK) {
    set_state(NFC_APDU_LAB_ERROR, HB_NFC_ERR_FIELD);
    goto done;
  }
  if (nfc_poller_start() != HB_NFC_OK) {
    set_state(NFC_APDU_LAB_ERROR, HB_NFC_ERR_FIELD);
    goto done;
  }
  field = true;
  vTaskDelay(pdMS_TO_TICKS(APDU_LAB_FIELD_SETTLE_MS));
  publish_rf_diagnostics();

  set_state(NFC_APDU_LAB_SEARCHING, HB_NFC_OK);
  hb_nfc_err_t err = activate_type_a(&card_a, &dep_a);
  if (err != HB_NFC_OK) {
    ESP_LOGW(TAG,
             "Type A activation failed at %umV; retrying with automatic RF regulator",
             APDU_LAB_RF_REGULATOR_MV);
    hb_nfc_err_t rf_err = switch_rf_to_auto();
    if (rf_err == HB_NFC_OK)
      err = activate_type_a(&card_a, &dep_a);
  }
  if (err == HB_NFC_OK) {
    channel.proto = HB_PROTO_ISO14443_4A;
    channel.ctx = &dep_a;
    channel.timeout_ms = APDU_LAB_TIMEOUT_MS;
    publish_link_a(&card_a, &dep_a);
  } else {
    set_state(NFC_APDU_LAB_ACTIVATING, err);
    err = iso14443b_poller_init();
    if (err == HB_NFC_OK)
      err = iso14443b_select(&card_b, 0x00U, 0x00U);
    if (err != HB_NFC_OK) {
      set_state(NFC_APDU_LAB_ERROR, err);
      goto done;
    }
    channel.proto = HB_PROTO_ISO14443_4B;
    channel.ctx = &card_b;
    channel.timeout_ms = APDU_LAB_TIMEOUT_MS;
    publish_link_b(&card_b);
  }

  set_state(NFC_APDU_LAB_READY, HB_NFC_OK);
  for (;;) {
    apdu_lab_request_t request;
    if (xQueueReceive(s_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE)
      continue;
    if (request.stop)
      break;

    set_state(NFC_APDU_LAB_EXCHANGING, HB_NFC_OK);
    nfc_apdu_lab_entry_t entry = {
        .tx_len = request.command_len,
        .risk = request.risk,
    };
    memcpy(entry.tx, request.command, request.command_len);
    nfc_apdu_resp_t response = {
        .buf = entry.rx,
        .max = sizeof(entry.rx),
    };
    entry.error = nfc_apdu_transceive(&channel, request.command, request.command_len, &response);
    if (response.sw == 0U &&
        (entry.error == HB_NFC_ERR_TIMEOUT || entry.error == HB_NFC_ERR_PROTOCOL)) {
      ESP_LOGW(TAG,
               "No APDU response at manual RF power; switching to automatic regulator and "
               "reactivating ISO-DEP");
      set_state(NFC_APDU_LAB_ACTIVATING, entry.error);
      err = switch_rf_to_auto();
      if (err == HB_NFC_OK)
        err = recover_session(&channel, &card_a, &dep_a, &card_b);
      if (err == HB_NFC_OK) {
        entry.session_recovered = true;
        set_state(NFC_APDU_LAB_EXCHANGING, HB_NFC_OK);
        entry.error =
            nfc_apdu_transceive(&channel, request.command, request.command_len, &response);
      } else {
        entry.error = err;
      }
    }
    entry.rx_len = response.len;
    entry.sw = response.sw;
    entry.elapsed_us = response.elapsed_us;
    entry.exchanges = response.exchanges;
    entry.le_corrected = response.le_corrected;
    entry.truncated = response.truncated;
    publish_entry(&entry, channel.proto);
    set_state(NFC_APDU_LAB_READY, entry.error);
  }

done:
  set_state(NFC_APDU_LAB_STOPPING, s_status.error);
  if (field)
    nfc_poller_stop();
  else if (highboy_nfc_is_initialized())
    (void)highboy_nfc_field_off();
  if (no_sleep)
    (void)power_manager_no_sleep_release();
  portENTER_CRITICAL(&s_mux);
  s_task = NULL;
  s_status.state = NFC_APDU_LAB_STOPPED;
  portEXIT_CRITICAL(&s_mux);
  vTaskDelete(NULL);
}

hb_nfc_err_t nfc_apdu_lab_start(void) {
  portENTER_CRITICAL(&s_mux);
  if (s_task != NULL) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  memset(&s_status, 0, sizeof(s_status));
  memset(s_history, 0, sizeof(s_history));
  s_history_next = 0;
  s_sequence = 0;
  s_status.state = NFC_APDU_LAB_STARTING;
  portEXIT_CRITICAL(&s_mux);

  if (s_queue == NULL) {
    s_queue = xQueueCreate(APDU_LAB_QUEUE_DEPTH, sizeof(apdu_lab_request_t));
    if (s_queue == NULL) {
      set_state(NFC_APDU_LAB_ERROR, ESP_ERR_NO_MEM);
      return ESP_ERR_NO_MEM;
    }
  } else {
    xQueueReset(s_queue);
  }
  if (xTaskCreatePinnedToCore(apdu_lab_task,
                              "nfc_apdu_lab",
                              APDU_LAB_TASK_STACK,
                              NULL,
                              SYS_PRIO_SERVICE_HI,
                              &s_task,
                              SYS_CORE_RADIO) != pdPASS) {
    s_task = NULL;
    set_state(NFC_APDU_LAB_ERROR, ESP_ERR_NO_MEM);
    return ESP_ERR_NO_MEM;
  }
  return HB_NFC_OK;
}

void nfc_apdu_lab_stop(void) {
  TaskHandle_t task;
  portENTER_CRITICAL(&s_mux);
  task = s_task;
  portEXIT_CRITICAL(&s_mux);
  if (task == NULL)
    return;

  apdu_lab_request_t request = {.stop = true};
  if (s_queue != NULL)
    (void)xQueueSend(s_queue, &request, pdMS_TO_TICKS(100));
  for (;;) {
    portENTER_CRITICAL(&s_mux);
    task = s_task;
    portEXIT_CRITICAL(&s_mux);
    if (task == NULL)
      break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

hb_nfc_err_t nfc_apdu_lab_submit(const uint8_t *apdu, size_t apdu_len, bool confirmed) {
  if (apdu == NULL || apdu_len == 0U || apdu_len > NFC_APDU_LAB_TX_MAX || s_queue == NULL)
    return HB_NFC_ERR_PARAM;
  nfc_apdu_cmd_info_t parsed;
  if (!nfc_apdu_parse_command(apdu, apdu_len, &parsed))
    return HB_NFC_ERR_PARAM;
  const nfc_apdu_risk_t risk = nfc_apdu_classify(&parsed);
  if (risk == NFC_APDU_RISK_PAYMENT)
    return ESP_ERR_NOT_SUPPORTED;
  if (risk != NFC_APDU_RISK_READ_ONLY && !confirmed)
    return ESP_ERR_INVALID_STATE;

  portENTER_CRITICAL(&s_mux);
  const bool ready = s_status.state == NFC_APDU_LAB_READY;
  portEXIT_CRITICAL(&s_mux);
  if (!ready)
    return ESP_ERR_INVALID_STATE;

  apdu_lab_request_t request = {
      .command_len = apdu_len,
      .risk = risk,
  };
  memcpy(request.command, apdu, apdu_len);
  if (xQueueSend(s_queue, &request, 0) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  return HB_NFC_OK;
}

void nfc_apdu_lab_get_status(nfc_apdu_lab_status_t *out) {
  if (out == NULL)
    return;
  portENTER_CRITICAL(&s_mux);
  *out = s_status;
  portEXIT_CRITICAL(&s_mux);
}

bool nfc_apdu_lab_get_history(uint8_t newest_index, nfc_apdu_lab_entry_t *out) {
  if (out == NULL)
    return false;
  bool ok = false;
  portENTER_CRITICAL(&s_mux);
  if (newest_index < s_status.history_count) {
    const uint8_t index =
        (uint8_t)((s_history_next + NFC_APDU_LAB_HISTORY_MAX - 1U - newest_index) %
                  NFC_APDU_LAB_HISTORY_MAX);
    *out = s_history[index];
    ok = true;
  }
  portEXIT_CRITICAL(&s_mux);
  return ok;
}

void nfc_apdu_lab_clear_history(void) {
  portENTER_CRITICAL(&s_mux);
  memset(s_history, 0, sizeof(s_history));
  s_history_next = 0;
  s_status.history_count = 0;
  s_status.latest_sequence = 0;
  portEXIT_CRITICAL(&s_mux);
}

const char *nfc_apdu_lab_trace_path(void) {
  return NFC_APDU_LAB_TRACE_PATH;
}
