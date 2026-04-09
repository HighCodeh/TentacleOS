// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "bt_dispatcher.h"

#include <string.h>

#include "esp_log.h"

#include "ble_connect_flood.h"
#include "ble_scanner.h"
#include "ble_sniffer.h"
#include "bluetooth_service.h"
#include "skimmer_detector.h"
#include "spi_bridge.h"
#include "tracker_detector.h"

static const char *TAG = "BT_DISPATCHER";

#define BT_SCAN_DEFAULT_DURATION_MS 5000
#define BT_CONNECT_MIN_PAYLOAD      7
#define BT_GET_INFO_RESP_LEN        7
#define BT_MAC_LEN                  6

spi_status_t bt_dispatcher_execute(spi_id_t id,
                                   const uint8_t *payload,
                                   uint8_t len,
                                   uint8_t *out_resp_payload,
                                   uint8_t *out_resp_len) {
  (void)TAG;
  *out_resp_len = 0;

  switch (id) {
    case SPI_ID_BT_SCAN: {
      uint32_t duration = BT_SCAN_DEFAULT_DURATION_MS;
      if (len >= sizeof(duration))
        memcpy(&duration, payload, sizeof(duration));
      bluetooth_service_scan(duration);
      spi_bridge_provide_results(bluetooth_service_get_scan_result(0),
                                 bluetooth_service_get_scan_count(),
                                 sizeof(bluetooth_service_scan_result_t));
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_CONNECT: {
      if (len < BT_CONNECT_MIN_PAYLOAD)
        return SPI_STATUS_ERROR;
      return (bluetooth_service_connect(payload, payload[BT_MAC_LEN], NULL) == ESP_OK)
                 ? SPI_STATUS_OK
                 : SPI_STATUS_ERROR;
    }

    case SPI_ID_BT_DISCONNECT:
      bluetooth_service_disconnect_all();
      return SPI_STATUS_OK;

    case SPI_ID_BT_GET_INFO: {
      bluetooth_service_get_mac(out_resp_payload);
      out_resp_payload[BT_MAC_LEN] = bluetooth_service_is_running() ? 1 : 0;
      *out_resp_len = BT_GET_INFO_RESP_LEN;
      return SPI_STATUS_OK;
    }

    case SPI_ID_BT_APP_SCANNER:
      return ble_scanner_start() ? SPI_STATUS_OK : SPI_STATUS_BUSY;

    case SPI_ID_BT_APP_SNIFFER:
      return (ble_sniffer_start() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_APP_FLOOD: {
      if (len < BT_CONNECT_MIN_PAYLOAD)
        return SPI_STATUS_ERROR;
      return (ble_connect_flood_start(payload, payload[BT_MAC_LEN]) == ESP_OK) ? SPI_STATUS_OK
                                                                               : SPI_STATUS_ERROR;
    }

    case SPI_ID_BT_APP_SKIMMER:
      return (skimmer_detector_start() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_APP_TRACKER:
      return (tracker_detector_start() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_BT_APP_STOP:
      ble_sniffer_stop();
      ble_connect_flood_stop();
      skimmer_detector_stop();
      tracker_detector_stop();
      return SPI_STATUS_OK;

    default:
      return SPI_STATUS_ERROR;
  }
}
