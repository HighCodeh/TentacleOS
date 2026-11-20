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

#include "rf_tag_test.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "highboy_nfc.h"
#include "led_control.h"
#include "lf_rfid.h"
#include "nfc_manager.h"
#include "rf_frontend.h"
#include "rf_passive.h"
#include "sys_prio.h"

#define WORKER_STACK     6144U
#define LF_READ_MS       500U
#define CL_MEASURE_MS    100U
#define POLL_GAP_MS      40U
#define CLEANUP_RETRY_MS 500U
#define US_PER_MS        1000U
static const char *TAG = "RF_TAG_TEST";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rf_tag_test_status_t s_status;
static bool s_cancel;

void rf_tag_test_snapshot(rf_tag_test_status_t *out) {
  if (out == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_status;
  portEXIT_CRITICAL(&s_lock);
}
bool rf_tag_test_busy(void) {
  rf_tag_test_status_t status;
  rf_tag_test_snapshot(&status);
  return status.busy;
}
void rf_tag_test_stop(void) {
  portENTER_CRITICAL(&s_lock);
  s_cancel = true;
  portEXIT_CRITICAL(&s_lock);
}
static bool cancelled(void) {
  portENTER_CRITICAL(&s_lock);
  bool stop = s_cancel;
  portEXIT_CRITICAL(&s_lock);
  return stop;
}
static void publish(const rf_tag_test_status_t *status) {
  portENTER_CRITICAL(&s_lock);
  s_status = *status;
  portEXIT_CRITICAL(&s_lock);
}
static esp_err_t read_nfc(rf_tag_test_status_t *status) {
  hb_nfc_card_data_t card = {0};
  esp_err_t error = nfc_manager_poll(status->tech, &card);
  if (error != ESP_OK)
    return error;
  const uint8_t *uid = NULL;
  switch (status->tech) {
    case NFC_MANAGER_TECH_A:
      uid = card.iso14443a.uid;
      status->uid_len = card.iso14443a.uid_len;
      memcpy(status->atqa, card.iso14443a.atqa, sizeof(status->atqa));
      status->sak = card.iso14443a.sak;
      break;
    case NFC_MANAGER_TECH_B:
      uid = card.data.iso14443b.pupi;
      status->uid_len = sizeof(card.data.iso14443b.pupi);
      break;
    case NFC_MANAGER_TECH_F:
      uid = card.data.felica.idm;
      status->uid_len = sizeof(card.data.felica.idm);
      break;
    case NFC_MANAGER_TECH_V:
      uid = card.data.iso15693.uid;
      status->uid_len = sizeof(card.data.iso15693.uid);
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }
  if (status->uid_len == 0 || status->uid_len > sizeof(status->uid))
    return ESP_ERR_INVALID_SIZE;
  memcpy(status->uid, uid, status->uid_len);
  status->protocol = card.protocol;
  return ESP_OK;
}
static void worker(void *arg) {
  (void)arg;
  rf_tag_test_status_t status;
  rf_tag_test_snapshot(&status);
  rf_frontend_mode_t mode =
      status.kind == RF_TAG_TEST_NFC ? RF_FRONTEND_NFC_13MHZ : RF_FRONTEND_LF_125KHZ;
  bool reserved = false;
  bool initialized = false;
  int64_t started = esp_timer_get_time();
  status.error = rf_frontend_reserve(mode);
  reserved = status.error == ESP_OK;
  if (reserved && !cancelled()) {
    if (status.kind == RF_TAG_TEST_NFC) {
      highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
      status.error = highboy_nfc_init(&config);
      initialized = highboy_nfc_is_initialized();
    } else {
      status.error = lf_rfid_init();
      initialized = status.error == ESP_OK;
      if (status.error == ESP_OK && !cancelled())
        status.error = lf_rfid_field_start(LF_RFID_NOMINAL_HZ, LF_RFID_DEFAULT_DUTY_PERMILLE);
      if (status.error == ESP_OK && !cancelled()) {
        lf_rfid_field_report_t report = {0};
        status.error = lf_rfid_measure(CL_MEASURE_MS, &report);
        if (status.error == ESP_OK)
          status.cl_hz = report.measured_hz;
      }
    }
  }
  while (status.error == ESP_OK && !cancelled()) {
    status.elapsed_ms = (esp_timer_get_time() - started) / US_PER_MS;
    if (status.elapsed_ms >= RF_TAG_TEST_DURATION_MS)
      break;
    status.phase = RF_TAG_TEST_READING;
    publish(&status);
    status.attempts++;
    if (status.kind == RF_TAG_TEST_NFC) {
      status.error = read_nfc(&status);
    } else {
      lf_rfid_em4100_t tag = {0};
      status.error = lf_rfid_read_em4100(LF_READ_MS, &tag);
      lf_rfid_read_stats_t stats;
      lf_rfid_read_stats(&stats);
      status.da_edges += stats.edges;
      status.dropped += stats.dropped;
      status.invalid_pulses += stats.invalid_pulses;
      if (status.error == ESP_OK) {
        status.uid_len = LF_RFID_EM4100_BYTES;
        memcpy(status.uid, tag.bytes, status.uid_len);
      }
    }
    if (status.error != ESP_ERR_NOT_FOUND)
      break;
    status.error = ESP_OK;
    if (status.kind == RF_TAG_TEST_NFC)
      status.tech = (status.tech + 1) % NFC_MANAGER_TECH_COUNT;
    vTaskDelay(pdMS_TO_TICKS(POLL_GAP_MS));
  }
  rf_tag_test_phase_t final_phase = cancelled()              ? RF_TAG_TEST_CANCELLED
                                    : status.error != ESP_OK ? RF_TAG_TEST_ERROR
                                    : status.uid_len         ? RF_TAG_TEST_FOUND
                                                             : RF_TAG_TEST_TIMEOUT;
  status.elapsed_ms = (esp_timer_get_time() - started) / US_PER_MS;
  do {
    status.cleanup_error = ESP_OK;
    if (reserved) {
      if (status.kind == RF_TAG_TEST_LF)
        status.cleanup_error = lf_rfid_field_stop_checked();
      else if (initialized)
        status.cleanup_error = highboy_nfc_field_off();
      if (status.cleanup_error == ESP_OK) {
        status.cleanup_error = rf_frontend_unreserve(mode);
        if (status.cleanup_error == ESP_OK)
          reserved = false;
      }
    }
    if (status.cleanup_error != ESP_OK) {
      if (status.phase != RF_TAG_TEST_HELD) {
        ESP_LOGE(TAG, "OFF unconfirmed: %s", esp_err_to_name(status.cleanup_error));
        led_signal_error();
      }
      status.phase = RF_TAG_TEST_HELD;
      publish(&status);
      vTaskDelay(pdMS_TO_TICKS(CLEANUP_RETRY_MS));
    }
  } while (status.cleanup_error != ESP_OK);
  status.phase = final_phase;
  status.busy = false;
  publish(&status);
  if (final_phase == RF_TAG_TEST_FOUND) {
    ESP_LOGI(TAG, "Tag read: kind=%d UID bytes=%u", status.kind, status.uid_len);
    led_signal_info();
  } else if (final_phase == RF_TAG_TEST_ERROR) {
    ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(status.error));
    led_signal_error();
  } else if (final_phase == RF_TAG_TEST_TIMEOUT) {
    ESP_LOGW(TAG, "No valid tag in %lu ms", (unsigned long)status.elapsed_ms);
    led_signal_warning();
  }
  vTaskDelete(NULL);
}
esp_err_t rf_tag_test_start(rf_tag_test_kind_t kind) {
  if ((unsigned)kind >= RF_TAG_TEST_KIND_COUNT)
    return ESP_ERR_INVALID_ARG;
  rf_passive_status_t bench = {0};
  rf_passive_snapshot(&bench);
  if (!rf_passive_is_boot() || bench.error != ESP_OK)
    return ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&s_lock);
  bool busy = s_status.busy;
  if (!busy) {
    s_cancel = false;
    s_status = (rf_tag_test_status_t){.kind = kind, .phase = RF_TAG_TEST_STARTING, .busy = true};
  }
  portEXIT_CRITICAL(&s_lock);
  if (busy)
    return ESP_ERR_INVALID_STATE;
  if (xTaskCreatePinnedToCore(
          worker, "rf_tag_test", WORKER_STACK, NULL, SYS_PRIO_SERVICE_LO, NULL, SYS_CORE_RADIO) !=
      pdPASS) {
    portENTER_CRITICAL(&s_lock);
    s_status.busy = false;
    s_status.phase = RF_TAG_TEST_ERROR;
    s_status.error = ESP_ERR_NO_MEM;
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "Active %s read requested", kind == RF_TAG_TEST_NFC ? "NFC" : "RFID RF/64");
  led_signal_info();
  return ESP_OK;
}
