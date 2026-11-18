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

#include "wardrive_radio.h"
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "spi_bridge.h"
#include "wifi_service.h"
static const char *TAG = "WARD_RADIO";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static spi_ward_status_t s_status;
EXT_RAM_BSS_ATTR static wifi_ap_record_t s_records[SPI_WARD_MAX_RECORDS];
static void scan(void) {
  uint16_t count = 0, total = 0;
  portENTER_CRITICAL(&s_lock);
  uint8_t band = s_status.band;
  portEXIT_CRITICAL(&s_lock);
  esp_err_t err = wifi_service_ward_scan(band, s_records, &count, &total);
  portENTER_CRITICAL(&s_lock);
  s_status.error = err;
  s_status.count = count;
  s_status.total = total;
  s_status.busy = 0;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGI(TAG, "band=%u count=%u total=%u result=%s", band, count, total, esp_err_to_name(err));
}
spi_status_t
wardrive_radio_command(spi_id_t id, const uint8_t *p, uint8_t len, uint8_t *out, uint8_t *out_len) {
  if (id == SPI_ID_WIFI_WARD_SCAN) {
    if (len != 1 || p[0] > SPI_WIFI_BAND_5G)
      return SPI_STATUS_INVALID_ARG;
    if (spi_bridge_async_scan_busy())
      return SPI_STATUS_BUSY;
    portENTER_CRITICAL(&s_lock);
    s_status.generation++;
    s_status.band = p[0];
    s_status.busy = 1;
    s_status.count = s_status.total = 0;
    s_status.error = ESP_OK;
    portEXIT_CRITICAL(&s_lock);
    if (!spi_bridge_async_scan_start(scan)) {
      portENTER_CRITICAL(&s_lock);
      s_status.busy = 0;
      s_status.error = ESP_ERR_NO_MEM;
      portEXIT_CRITICAL(&s_lock);
      return SPI_STATUS_ERROR;
    }
  } else if (id == SPI_ID_WIFI_WARD_STATUS) {
    if (len != 0)
      return SPI_STATUS_INVALID_ARG;
  } else if (id == SPI_ID_WIFI_WARD_RECORD) {
    if (len != sizeof(spi_ward_request_t))
      return SPI_STATUS_INVALID_ARG;
    spi_ward_request_t request;
    memcpy(&request, p, sizeof(request));
    portENTER_CRITICAL(&s_lock);
    if (s_status.busy || s_status.error || request.generation != s_status.generation ||
        request.index >= s_status.count) {
      portEXIT_CRITICAL(&s_lock);
      return SPI_STATUS_INVALID_ARG;
    }
    wifi_ap_record_t ap = s_records[request.index];
    portEXIT_CRITICAL(&s_lock);
    spi_ward_record_t rec = {.channel = ap.primary, .authmode = ap.authmode, .rssi = ap.rssi};
    memcpy(rec.bssid, ap.bssid, sizeof(rec.bssid));
    memcpy(rec.ssid, ap.ssid, sizeof(rec.ssid));
    rec.ssid[sizeof(rec.ssid) - 1] = 0;
    memcpy(out, &rec, sizeof(rec));
    *out_len = sizeof(rec);
    return SPI_STATUS_OK;
  } else
    return SPI_STATUS_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  memcpy(out, &s_status, sizeof(s_status));
  *out_len = sizeof(s_status);
  portEXIT_CRITICAL(&s_lock);
  return SPI_STATUS_OK;
}
