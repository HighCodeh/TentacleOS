// SPDX-License-Identifier: GPL-3.0-or-later
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/task.h"
#include "spi_bridge.h"
#include "wifi_scan_view.h"

static void (*worker)(void *);
static bool fail_task, short_reply, cancel_in_scan, short_record, unknown_oui;
static unsigned scans, records;
static esp_err_t scan_error, data_error;
static int64_t now_us;
static uint16_t result_count = 2;
static int8_t ap_rssi = -50;

int64_t esp_timer_get_time(void) { return now_us; }
void vTaskDelete(TaskHandle_t task) { assert(task == NULL); }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack,
                                  void *ctx, unsigned prio, TaskHandle_t *out, unsigned core) {
  (void)name; (void)ctx;
  assert(stack >= 4096 && prio == 4 && core == 0 && out == NULL);
  worker = fn; return fail_task ? 0 : pdPASS;
}
esp_err_t spi_bridge_run_scan(spi_id_t scan, spi_id_t status, const uint8_t *payload, uint8_t len) {
  assert(scan == SPI_ID_WIFI_APP_SCAN_AP && status == SPI_ID_WIFI_SCAN_STATUS);
  assert(payload == NULL && len == 0);
  scans++; now_us += 4000000;
  if (cancel_in_scan) wifi_scan_view_cancel();
  return scan_error;
}
esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *req, uint8_t len,
                                  spi_header_t *header, uint8_t *out, size_t capacity,
                                  uint32_t timeout_ms) {
  (void)timeout_ms;
  // In particular, no STOP/START/QUICK_START is permitted by this flow.
  assert(id == SPI_ID_SYSTEM_DATA && len == sizeof(uint16_t));
  if (data_error != ESP_OK) return data_error;
  uint16_t index; memcpy(&index, req, sizeof(index));
  if (index == SPI_DATA_INDEX_COUNT) {
    assert(capacity == sizeof(result_count));
    memcpy(out, &result_count, sizeof(result_count));
    header->length = short_reply ? 0 : sizeof(result_count);
  } else {
    assert(capacity == sizeof(spi_wifi_scan_record_t));
    spi_wifi_scan_record_t ap = {.bssid = {0x10, 0xbd, 0xa3, 0xc0, 0x8c, 0xb9},
                                .rssi = -50, .channel = 6};
    if (index == 0) { ap.bssid[0] = 0x02; ap.rssi = -60; }
    if (index == 1) {
      memcpy(ap.ssid, "Darth Maul", 11);
      ap.rssi = ap_rssi;
      if (unknown_oui) ap.bssid[0] = 0x00;
    }
    memcpy(out, &ap, sizeof(ap)); records++;
    header->length = short_record ? 0 : sizeof(ap);
  }
  return ESP_OK;
}
static wifi_scan_view_status_t status(void) {
  wifi_scan_view_status_t value; wifi_scan_view_get_status(&value); return value;
}
static void reset(void) {
  fail_task = short_reply = cancel_in_scan = short_record = unknown_oui = false;
  scans = records = 0; now_us = 0; result_count = 2;
  ap_rssi = -50;
  scan_error = data_error = ESP_OK;
}
int main(void) {
  reset();
  assert(wifi_scan_view_start(31) == ESP_ERR_INVALID_ARG);
  assert(wifi_scan_view_start(45) == ESP_OK);
  assert(status().is_busy && wifi_scan_view_start(45) == ESP_ERR_INVALID_STATE);
  spi_wifi_quick_record_t record;
  assert(wifi_scan_view_get_record(0, &record) != ESP_OK);
  worker(NULL);
  assert(scans == 1 && records == 2 && !status().is_busy && status().error == ESP_OK);
  assert(status().capture.state == SPI_WIFI_QUICK_DONE && status().count == 2);
  assert(status().capture.elapsed_ms == 4000);
  assert(wifi_scan_view_get_record(0, &record) == ESP_OK && !strcmp(record.ssid, "Darth Maul"));
  assert(record.score == 100 && record.oui_points == 69 && record.proximity_points == 31);
  assert(record.frames == 0 && record.persistence_points == 0 && record.fingerprint_points == 0);
  assert(wifi_scan_view_get_record(1, &record) == ESP_OK && record.oui_points == 0);
  assert(record.ssid_state == SPI_WIFI_QUICK_SSID_HIDDEN && record.score == 16);
  assert(!record.should_investigate);
  reset(); unknown_oui = true; assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
  assert(wifi_scan_view_get_record(0, &record) == ESP_OK && !strcmp(record.ssid, "Darth Maul"));
  assert(record.score == 31 && record.oui_points == 0 && !record.should_investigate);
  const int8_t signals[] = {-55, -56, -65, -66};
  const uint8_t scores[] = {100, 85, 85, 69};
  for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
    reset(); ap_rssi = signals[i];
    assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
    assert(wifi_scan_view_get_record(0, &record) == ESP_OK && record.score == scores[i]);
  }
  reset(); assert(wifi_scan_view_start(45) == ESP_OK); wifi_scan_view_cancel(); worker(NULL);
  assert(scans == 0 && status().capture.state == SPI_WIFI_QUICK_CANCELLED);
  reset(); cancel_in_scan = true; assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
  assert(scans == 1 && records == 0 && status().capture.state == SPI_WIFI_QUICK_CANCELLED);
  reset(); scan_error = ESP_ERR_TIMEOUT; assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
  assert(scans == 1 && records == 0 && status().error == ESP_ERR_TIMEOUT);
  reset(); short_reply = true; assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
  assert(status().error == ESP_ERR_INVALID_SIZE && records == 0);
  reset(); short_record = true; assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
  assert(status().error == ESP_ERR_INVALID_SIZE && status().count == 0);
  reset(); data_error = ESP_FAIL; assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
  assert(status().error == ESP_FAIL);
  reset(); result_count = 0; assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
  assert(status().error == ESP_OK && status().count == 0);
  reset(); result_count = 129; assert(wifi_scan_view_start(45) == ESP_OK); worker(NULL);
  assert(status().count == 128 && records == 128 && status().capture.overflow_observations == 1);
  reset(); fail_task = true;
  assert(wifi_scan_view_start(45) == ESP_ERR_NO_MEM && !status().is_busy);
  return 0;
}
