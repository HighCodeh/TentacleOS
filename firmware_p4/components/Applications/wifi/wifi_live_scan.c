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

#include "wifi_live_scan.h"

#include <stdatomic.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi_bridge.h"
#include "ap_scanner.h"
#include "sys_prio.h"

#define LIVE_STACK_SIZE      4096
#define LIVE_RPC_MS          1000
#define LIVE_POLL_MS         30
#define LIVE_STEP_TIMEOUT_US 5000000
#define LIVE_SSID_ASCII_MIN  0x20
#define LIVE_SSID_ASCII_MAX  0x7E

static const char *TAG = "WIFI_LIVE_SCAN";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
EXT_RAM_BSS_ATTR static wifi_ap_record_t s_records[WIFI_LIVE_SCAN_MAX_APS];
EXT_RAM_BSS_ATTR static wifi_live_scan_observation_t s_observations[WIFI_LIVE_SCAN_MAX_APS];
static wifi_live_scan_status_t s_status;
static atomic_bool s_stop;
static uint8_t s_band;

static void worker(void *arg);
static void merge_record(wifi_ap_record_t *record);
static esp_err_t scan_step(uint8_t slot);

esp_err_t wifi_live_scan_start(uint8_t band) {
  if (band > SPI_WIFI_BAND_5G)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  if (s_status.is_running) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  s_status = (wifi_live_scan_status_t){.is_running = true, .revision = s_status.revision + 1};
  s_band = band;
  atomic_store(&s_stop, false);
  portEXIT_CRITICAL(&s_lock);
  if (xTaskCreatePinnedToCore(
          worker, "wifi_live", LIVE_STACK_SIZE, NULL, SYS_PRIO_SERVICE_LO, NULL, SYS_CORE_RADIO) ==
      pdPASS)
    return ESP_OK;
  ESP_LOGE(TAG, "Failed to allocate live scan task");
  portENTER_CRITICAL(&s_lock);
  s_status.is_running = false;
  s_status.error = ESP_ERR_NO_MEM;
  portEXIT_CRITICAL(&s_lock);
  return ESP_ERR_NO_MEM;
}

void wifi_live_scan_stop(void) {
  atomic_store(&s_stop, true);
}

void wifi_live_scan_get_status(wifi_live_scan_status_t *out_status) {
  if (out_status == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out_status = s_status;
  portEXIT_CRITICAL(&s_lock);
}

esp_err_t wifi_live_scan_get_record(uint16_t index, wifi_ap_record_t *out_record) {
  if (out_record == NULL)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  bool has_record = index < s_status.count;
  if (has_record)
    *out_record = s_records[index];
  portEXIT_CRITICAL(&s_lock);
  return has_record ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_live_scan_get_observation(uint16_t index, wifi_live_scan_observation_t *out_stats) {
  if (out_stats == NULL)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  bool has_record = index < s_status.count;
  if (has_record)
    *out_stats = s_observations[index];
  portEXIT_CRITICAL(&s_lock);
  return has_record ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void merge_record(wifi_ap_record_t *record) {
  record->ssid[sizeof(record->ssid) - 1] = '\0';
  for (size_t i = 0; record->ssid[i] != '\0'; i++)
    if (record->ssid[i] < LIVE_SSID_ASCII_MIN || record->ssid[i] > LIVE_SSID_ASCII_MAX)
      record->ssid[i] = '?';
  portENTER_CRITICAL(&s_lock);
  uint16_t at = 0;
  while (at < s_status.count &&
         memcmp(s_records[at].bssid, record->bssid, sizeof(record->bssid)) != 0)
    at++;
  if (at < WIFI_LIVE_SCAN_MAX_APS) {
    // A hidden beacon must not erase a previously discovered SSID.
    if (at < s_status.count && record->ssid[0] == '\0')
      memcpy(record->ssid, s_records[at].ssid, sizeof(record->ssid));
    wifi_live_scan_observation_t *stats = &s_observations[at];
    if (at == s_status.count)
      *stats = (wifi_live_scan_observation_t){.rssi_min = record->rssi, .rssi_max = record->rssi};
    if (record->rssi < stats->rssi_min)
      stats->rssi_min = record->rssi;
    if (record->rssi > stats->rssi_max)
      stats->rssi_max = record->rssi;
    if (stats->samples < UINT16_MAX) {
      stats->rssi_sum += record->rssi;
      stats->samples++;
    }
    s_records[at] = *record;
    if (at == s_status.count)
      s_status.count++;
    s_status.revision++;
  } else {
    s_status.is_full = true;
  }
  portEXIT_CRITICAL(&s_lock);
}

static esp_err_t scan_step(uint8_t slot) {
  esp_err_t err = spi_bridge_send_command(
      SPI_ID_WIFI_SCAN_STEP, &slot, sizeof(slot), NULL, NULL, 0, LIVE_RPC_MS);
  if (err != ESP_OK)
    return err;
  int64_t deadline = esp_timer_get_time() + LIVE_STEP_TIMEOUT_US;
  spi_wifi_scan_step_status_t status = {.busy = 1};
  // Drain the current bounded channel before releasing ownership, even on Back.
  do {
    vTaskDelay(pdMS_TO_TICKS(LIVE_POLL_MS));
    spi_header_t response = {0};
    err = spi_bridge_send_command(SPI_ID_WIFI_SCAN_STEP_STATUS,
                                  NULL,
                                  0,
                                  &response,
                                  (uint8_t *)&status,
                                  sizeof(status),
                                  LIVE_RPC_MS);
    if (err != ESP_OK)
      return err;
    if (response.length != sizeof(status))
      return ESP_ERR_INVALID_SIZE;
    if (esp_timer_get_time() >= deadline)
      return ESP_ERR_TIMEOUT;
  } while (status.busy);
  // A bitmap containing no channels permitted by the country is skipped.
  if (status.error == ESP_ERR_INVALID_ARG)
    return ESP_OK;
  if (status.error != ESP_OK)
    return (esp_err_t)status.error;
  if (status.count > WIFI_SCAN_LIST_SIZE)
    return ESP_ERR_INVALID_SIZE;
  for (uint16_t i = 0; i < status.count; i++) {
    if (atomic_load(&s_stop))
      break;
    spi_wifi_scan_record_t record;
    spi_header_t response = {0};
    err = spi_bridge_send_command(SPI_ID_WIFI_SCAN_STEP_RECORD,
                                  (const uint8_t *)&i,
                                  sizeof(i),
                                  &response,
                                  (uint8_t *)&record,
                                  sizeof(record),
                                  LIVE_RPC_MS);
    if (err != ESP_OK)
      return err;
    if (response.length != sizeof(record))
      return ESP_ERR_INVALID_SIZE;
    wifi_ap_record_t ap = {0};
    memcpy(ap.ssid, record.ssid, sizeof(ap.ssid));
    memcpy(ap.bssid, record.bssid, sizeof(ap.bssid));
    ap.primary = record.channel;
    ap.rssi = record.rssi;
    ap.authmode = record.authmode;
    merge_record(&ap);
  }
  return ESP_OK;
}

static void worker(void *arg) {
  (void)arg;
  esp_err_t err = ESP_OK;
  uint8_t first = s_band == SPI_WIFI_BAND_5G ? SPI_WIFI_SCAN_2G_SLOTS : 0;
  uint8_t end = s_band == SPI_WIFI_BAND_2G ? SPI_WIFI_SCAN_2G_SLOTS : SPI_WIFI_SCAN_SLOT_COUNT;
  while (!atomic_load(&s_stop)) {
    for (uint8_t slot = first; slot < end && !atomic_load(&s_stop); slot++) {
      err = scan_step(slot);
      if (err != ESP_OK)
        goto cleanup;
    }
    portENTER_CRITICAL(&s_lock);
    s_status.sweeps++;
    portEXIT_CRITICAL(&s_lock);
  }
cleanup:
  (void)ap_scanner_save_snapshot(s_records, s_status.count);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "Live scan stopped: %s", esp_err_to_name(err));
  portENTER_CRITICAL(&s_lock);
  s_status.error = err;
  s_status.is_running = false;
  s_status.revision++;
  portEXIT_CRITICAL(&s_lock);
  vTaskDelete(NULL);
}
