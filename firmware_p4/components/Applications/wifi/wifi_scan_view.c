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

#include "wifi_scan_view.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "spi_bridge.h"
#include "sys_prio.h"
#include "mac_vendor.h"

#define SCAN_VIEW_MAX_RECORDS   128
#define SCAN_VIEW_STACK_SIZE    4096
#define SCAN_VIEW_RPC_MS        1000
#define SCAN_VIEW_US_PER_MS     1000
#define SCAN_VIEW_MS_PER_SECOND 1000
#define SCAN_VIEW_SHORT_SECONDS 30
#define SCAN_VIEW_LONG_SECONDS  60
#define SCAN_SCORE_OUI          69
#define SCAN_SCORE_STRONG       31
#define SCAN_SCORE_NEAR         16
#define SCAN_RSSI_STRONG        (-55)
#define SCAN_RSSI_NEAR          (-65)
#define SCAN_MAC_LOCAL_BIT      0x02
#define SCAN_ASCII_MIN          0x20
#define SCAN_ASCII_MAX          0x7E

static const char *TAG = "WIFI_SCAN_VIEW";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_scan_view_status_t s_status;
EXT_RAM_BSS_ATTR static spi_wifi_quick_record_t s_records[SCAN_VIEW_MAX_RECORDS];
static atomic_bool s_cancel;
static uint8_t s_seconds;

static esp_err_t start_job(uint8_t seconds);
static void worker(void *arg);
static esp_err_t run_scan(uint8_t seconds);
static void classify_ap(const spi_wifi_scan_record_t *ap, spi_wifi_quick_record_t *out_record);

esp_err_t wifi_scan_view_start(uint8_t seconds) {
  if (seconds != SCAN_VIEW_SHORT_SECONDS && seconds != SPI_WIFI_QUICK_DEFAULT_SECONDS &&
      seconds != SCAN_VIEW_LONG_SECONDS) {
    return ESP_ERR_INVALID_ARG;
  }
  return start_job(seconds);
}

void wifi_scan_view_cancel(void) {
  atomic_store(&s_cancel, true);
}

void wifi_scan_view_get_status(wifi_scan_view_status_t *out_status) {
  portENTER_CRITICAL(&s_lock);
  *out_status = s_status;
  portEXIT_CRITICAL(&s_lock);
}

esp_err_t wifi_scan_view_get_record(uint16_t index, spi_wifi_quick_record_t *out_record) {
  if (out_record == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  portENTER_CRITICAL(&s_lock);
  bool has_record = !s_status.is_busy && index < s_status.count;
  if (has_record) {
    *out_record = s_records[index];
  }
  portEXIT_CRITICAL(&s_lock);
  return has_record ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t start_job(uint8_t seconds) {
  portENTER_CRITICAL(&s_lock);
  if (s_status.is_busy) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  s_seconds = seconds;
  s_status.is_busy = true;
  s_status.error = ESP_OK;
  if (seconds != 0) {
    s_status.count = 0;
    memset(&s_status.capture, 0, sizeof(s_status.capture));
  }
  s_status.revision++;
  atomic_store(&s_cancel, false);
  portEXIT_CRITICAL(&s_lock);
  if (xTaskCreatePinnedToCore(worker,
                              "wifi_scan_view",
                              SCAN_VIEW_STACK_SIZE,
                              NULL,
                              SYS_PRIO_SERVICE_LO,
                              NULL,
                              SYS_CORE_RADIO) == pdPASS) {
    return ESP_OK;
  }
  ESP_LOGE(TAG, "Failed to create scan worker");
  portENTER_CRITICAL(&s_lock);
  s_status.error = ESP_ERR_NO_MEM;
  s_status.is_busy = false;
  s_status.revision++;
  portEXIT_CRITICAL(&s_lock);
  return ESP_ERR_NO_MEM;
}

static void worker(void *arg) {
  (void)arg;
  portENTER_CRITICAL(&s_lock);
  uint8_t seconds = s_seconds;
  portEXIT_CRITICAL(&s_lock);
  esp_err_t err = run_scan(seconds);
  portENTER_CRITICAL(&s_lock);
  s_status.error = err;
  s_status.is_busy = false;
  s_status.revision++;
  portEXIT_CRITICAL(&s_lock);
  vTaskDelete(NULL);
}

static esp_err_t run_scan(uint8_t seconds) {
  (void)seconds;
  int64_t started_us = esp_timer_get_time();
  spi_wifi_quick_status_t capture = {.state = SPI_WIFI_QUICK_RUNNING};
  esp_err_t err = ESP_OK;
  if (atomic_load(&s_cancel)) {
    capture.state = SPI_WIFI_QUICK_CANCELLED;
    goto cleanup;
  }
  // Use the same AP scan as the existing Wi-Fi apps; never enter monitor mode.
  err = spi_bridge_run_scan(SPI_ID_WIFI_APP_SCAN_AP, SPI_ID_WIFI_SCAN_STATUS, NULL, 0);
  if (err != ESP_OK) {
    goto cleanup;
  }
  if (atomic_load(&s_cancel)) {
    capture.state = SPI_WIFI_QUICK_CANCELLED;
    goto cleanup;
  }
  uint16_t index = SPI_DATA_INDEX_COUNT;
  uint16_t count = 0;
  spi_header_t response = {0};
  err = spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                                (const uint8_t *)&index,
                                sizeof(index),
                                &response,
                                (uint8_t *)&count,
                                sizeof(count),
                                SCAN_VIEW_RPC_MS);
  if (err != ESP_OK || response.length != sizeof(count)) {
    if (err == ESP_OK)
      err = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }
  if (count > SCAN_VIEW_MAX_RECORDS) {
    capture.overflow_observations = count - SCAN_VIEW_MAX_RECORDS;
    count = SCAN_VIEW_MAX_RECORDS;
  }
  for (index = 0; index < count; index++) {
    if (atomic_load(&s_cancel)) {
      capture.state = SPI_WIFI_QUICK_CANCELLED;
      goto cleanup;
    }
    spi_wifi_scan_record_t ap;
    response = (spi_header_t){0};
    err = spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                                  (const uint8_t *)&index,
                                  sizeof(index),
                                  &response,
                                  (uint8_t *)&ap,
                                  sizeof(ap),
                                  SCAN_VIEW_RPC_MS);
    if (err != ESP_OK || response.length != sizeof(ap)) {
      if (err == ESP_OK)
        err = ESP_ERR_INVALID_SIZE;
      goto cleanup;
    }
    spi_wifi_quick_record_t record;
    classify_ap(&ap, &record);
    portENTER_CRITICAL(&s_lock);
    uint16_t at = s_status.count;
    while (at > 0 && s_records[at - 1].score < record.score) {
      s_records[at] = s_records[at - 1];
      at--;
    }
    s_records[at] = record;
    s_status.count++;
    capture.total_count = s_status.count;
    capture.unknown_count = s_status.count;
    portEXIT_CRITICAL(&s_lock);
  }
  capture.state = SPI_WIFI_QUICK_DONE;
cleanup:
  capture.elapsed_ms = (uint32_t)((esp_timer_get_time() - started_us) / SCAN_VIEW_US_PER_MS);
  capture.error = (uint32_t)err;
  if (err != ESP_OK)
    capture.state = SPI_WIFI_QUICK_ERROR;
  portENTER_CRITICAL(&s_lock);
  s_status.capture = capture;
  portEXIT_CRITICAL(&s_lock);
  return err;
}

static void classify_ap(const spi_wifi_scan_record_t *ap, spi_wifi_quick_record_t *out_record) {
  memset(out_record, 0, sizeof(*out_record));
  memcpy(out_record->mac, ap->bssid, sizeof(out_record->mac));
  memcpy(out_record->bssid, ap->bssid, sizeof(out_record->bssid));
  out_record->channel = ap->channel;
  out_record->rssi_avg = ap->rssi;
  out_record->rssi_min = ap->rssi;
  out_record->rssi_max = ap->rssi;
  const char *vendor = mac_vendor_get_name(ap->bssid);
  if ((ap->bssid[0] & SCAN_MAC_LOCAL_BIT) != 0) {
    vendor = "Local/unknown";
    out_record->evidence = SPI_WIFI_QUICK_LOCAL_MAC;
  } else if (strcmp(vendor, "Espressif") == 0) {
    out_record->evidence = SPI_WIFI_QUICK_ESPRESSIF_OUI;
    out_record->oui_points = SCAN_SCORE_OUI;
  }
  if (vendor[0] == '\0')
    vendor = "Unknown";
  strncpy(out_record->vendor, vendor, sizeof(out_record->vendor) - 1);
  size_t length = 0;
  while (length < sizeof(out_record->ssid) - 1 && length < sizeof(ap->ssid) && ap->ssid[length]) {
    uint8_t c = (uint8_t)ap->ssid[length];
    out_record->ssid[length++] = c < SCAN_ASCII_MIN || c > SCAN_ASCII_MAX ? '?' : (char)c;
  }
  out_record->ssid_state = length ? SPI_WIFI_QUICK_SSID_KNOWN : SPI_WIFI_QUICK_SSID_HIDDEN;
  out_record->proximity_points = ap->rssi >= SCAN_RSSI_STRONG ? SCAN_SCORE_STRONG
                                 : ap->rssi >= SCAN_RSSI_NEAR ? SCAN_SCORE_NEAR
                                                              : 0;
  out_record->score = out_record->oui_points + out_record->proximity_points;
  out_record->should_investigate = out_record->oui_points != 0;
}
