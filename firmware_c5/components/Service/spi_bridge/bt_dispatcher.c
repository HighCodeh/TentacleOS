#include "bt_dispatcher.h"
#include "bluetooth_service.h"
#include "ble_scanner.h"
#include "ble_sniffer.h"
#include "ble_connect_flood.h"
#include "skimmer_detector.h"
#include "tracker_detector.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <string.h>

spi_status_t bt_dispatcher_execute(spi_id_t id, const uint8_t *payload, uint8_t len, 
                                  uint8_t *out_resp_payload, uint8_t *out_resp_len) {
    *out_resp_len = 0;

    switch (id) {
        case SPI_ID_BT_SCAN: {
            uint32_t duration = 5000;
            if (len >= 4) memcpy(&duration, payload, 4);
            bluetooth_service_scan(duration);
            // Point bridge to BT result set
            spi_bridge_provide_results(bluetooth_service_get_scan_result(0), bluetooth_service_get_scan_count(), sizeof(ble_scan_result_t));
            return SPI_STATUS_OK;
        }

        case SPI_ID_BT_CONNECT: {
            if (len < 7) return SPI_STATUS_ERROR;
            return (bluetooth_service_connect(payload, payload[6], NULL) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        }

        case SPI_ID_BT_DISCONNECT:
            bluetooth_service_disconnect_all();
            return SPI_STATUS_OK;

        case SPI_ID_BT_GET_INFO: {
            bluetooth_service_get_mac(out_resp_payload);
            out_resp_payload[6] = bluetooth_service_is_running() ? 1 : 0;
            *out_resp_len = 7;
            return SPI_STATUS_OK;
        }

        case SPI_ID_BT_APP_SCANNER:
            return ble_scanner_start() ? SPI_STATUS_OK : SPI_STATUS_BUSY;

        case SPI_ID_BT_APP_SNIFFER:
            return (ble_sniffer_start() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

        case SPI_ID_BT_APP_FLOOD: {
            if (len < 7) return SPI_STATUS_ERROR;
            return (ble_connect_flood_start(payload, payload[6]) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
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
