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

#include "wifi_quick_scan.h"

#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"

#define QUICK_RPC_TIMEOUT_MS 1000
#define QUICK_MAC_LEN        SPI_WIFI_QUICK_MAC_LEN

static const char *TAG = "WIFI_QUICK_SCAN";

typedef struct {
  spi_id_t id;
  const void *payload;
  uint8_t length;
} wifi_quick_scan_request_t;

_Static_assert(sizeof(spi_wifi_quick_record_t) <= SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE,
               "Quick scan record exceeds SPI response");

static esp_err_t
get_response(const wifi_quick_scan_request_t *request, void *out_response, uint8_t response_len);

esp_err_t wifi_quick_scan_init(void) {
  (void)TAG;
  return ESP_OK;
}

esp_err_t wifi_quick_scan_start(const spi_wifi_quick_config_t *config) {
  return spi_bridge_send_command(SPI_ID_WIFI_QUICK_START,
                                 (const uint8_t *)config,
                                 config == NULL ? 0 : sizeof(*config),
                                 NULL,
                                 NULL,
                                 0,
                                 QUICK_RPC_TIMEOUT_MS);
}

esp_err_t wifi_quick_scan_stop(void) {
  return spi_bridge_send_command(
      SPI_ID_WIFI_QUICK_STOP, NULL, 0, NULL, NULL, 0, QUICK_RPC_TIMEOUT_MS);
}

esp_err_t wifi_quick_scan_get_status(spi_wifi_quick_status_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  wifi_quick_scan_request_t request = {.id = SPI_ID_WIFI_QUICK_STATUS};
  return get_response(&request, out_status, sizeof(*out_status));
}

esp_err_t wifi_quick_scan_get_record(uint16_t index,
                                     bool include_authorized,
                                     spi_wifi_quick_record_t *out_record) {
  if (out_record == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  spi_wifi_quick_record_req_t request = {
      .index = index,
      .include_authorized = include_authorized,
  };
  wifi_quick_scan_request_t rpc = {
      .id = SPI_ID_WIFI_QUICK_RECORD,
      .payload = &request,
      .length = sizeof(request),
  };
  return get_response(&rpc, out_record, sizeof(*out_record));
}

esp_err_t wifi_quick_scan_set_authorized(const uint8_t mac[QUICK_MAC_LEN], bool is_authorized) {
  if (mac == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  spi_wifi_quick_whitelist_req_t request = {.is_authorized = is_authorized};
  memcpy(request.mac, mac, sizeof(request.mac));
  return spi_bridge_send_command(SPI_ID_WIFI_QUICK_WHITELIST,
                                 (const uint8_t *)&request,
                                 sizeof(request),
                                 NULL,
                                 NULL,
                                 0,
                                 QUICK_RPC_TIMEOUT_MS);
}

static esp_err_t
get_response(const wifi_quick_scan_request_t *request, void *out_response, uint8_t response_len) {
  spi_header_t response = {0};
  uint8_t payload[sizeof(spi_wifi_quick_record_t) + 1];
  esp_err_t err = spi_bridge_send_command(request->id,
                                          request->payload,
                                          request->length,
                                          &response,
                                          payload,
                                          response_len + 1,
                                          QUICK_RPC_TIMEOUT_MS);
  if (err != ESP_OK) {
    return err;
  }
  if (response.length != response_len) {
    memset(out_response, 0, response_len);
    return ESP_ERR_INVALID_SIZE;
  }
  memcpy(out_response, payload, response_len);
  return ESP_OK;
}
