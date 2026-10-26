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

#include <assert.h>
#include <string.h>

#include "wifi_quick_scan.h"

static spi_id_t last_id;
static uint8_t last_request[16];
static uint8_t last_length;
static int response_delta;
static esp_err_t transport_error;

esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *request, uint8_t length,
                                  spi_header_t *header, uint8_t *out, size_t capacity,
                                  uint32_t timeout_ms) {
  assert(timeout_ms == 1000);
  last_id = id;
  last_length = length;
  assert(length <= sizeof(last_request));
  if (length) {
    memcpy(last_request, request, length);
  }
  if (transport_error) {
    return transport_error;
  }
  if (header) {
    size_t expected = id == SPI_ID_WIFI_QUICK_STATUS ? sizeof(spi_wifi_quick_status_t)
                                                    : sizeof(spi_wifi_quick_record_t);
    size_t response_length = expected + response_delta;
    header->length = response_length > capacity ? capacity : response_length;
    memset(out, 0x5A, header->length);
  }
  return ESP_OK;
}

int main(void) {
  assert(wifi_quick_scan_init() == ESP_OK);
  assert(wifi_quick_scan_start(NULL) == ESP_OK);
  assert(last_id == SPI_ID_WIFI_QUICK_START && last_length == 0);
  spi_wifi_quick_config_t config = {.duration_seconds = 60, .band = SPI_WIFI_BAND_2G};
  assert(wifi_quick_scan_start(&config) == ESP_OK);
  assert(last_length == sizeof(config) && !memcmp(last_request, &config, sizeof(config)));
  assert(wifi_quick_scan_stop() == ESP_OK && last_id == SPI_ID_WIFI_QUICK_STOP);
  assert(wifi_quick_scan_get_status(NULL) == ESP_ERR_INVALID_ARG);
  assert(wifi_quick_scan_get_record(0, false, NULL) == ESP_ERR_INVALID_ARG);
  assert(wifi_quick_scan_set_authorized(NULL, true) == ESP_ERR_INVALID_ARG);
  uint8_t mac[6] = {0x24, 0x6F, 0x28, 1, 2, 3};
  assert(wifi_quick_scan_set_authorized(mac, true) == ESP_OK);
  assert(last_id == SPI_ID_WIFI_QUICK_WHITELIST && last_request[6] == 1);
  assert(!memcmp(last_request, mac, 6));
  spi_wifi_quick_record_t record;
  assert(wifi_quick_scan_get_record(12, true, &record) == ESP_OK);
  spi_wifi_quick_record_req_t request;
  memcpy(&request, last_request, sizeof(request));
  assert(request.index == 12 && request.include_authorized == 1);
  spi_wifi_quick_status_t status;
  assert(wifi_quick_scan_get_status(&status) == ESP_OK);
  for (int delta = -1; delta <= 1; delta += 2) {
    response_delta = delta;
    assert(wifi_quick_scan_get_status(&status) == ESP_ERR_INVALID_SIZE);
    assert(status.state == 0);
    assert(wifi_quick_scan_get_record(0, false, &record) == ESP_ERR_INVALID_SIZE);
    assert(record.frames == 0);
  }
  transport_error = ESP_ERR_NOT_SUPPORTED;
  assert(wifi_quick_scan_start(NULL) == ESP_ERR_NOT_SUPPORTED);
  assert(wifi_quick_scan_get_status(&status) == ESP_ERR_NOT_SUPPORTED);
  return 0;
}
