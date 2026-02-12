#include "wifi_deauther.h"
#include "spi_bridge.h"
#include <string.h>

bool wifi_deauther_start(const wifi_ap_record_t *ap_record, deauth_frame_type_t type, bool is_broadcast) {
    uint8_t payload[13];
    memcpy(payload, ap_record->bssid, 6);
    memset(payload + 6, is_broadcast ? 0xFF : 0x00, 6); // Broadcast or AP itself
    payload[12] = (uint8_t)type;
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_DEAUTHER, payload, 13, NULL, NULL, 2000) == ESP_OK);
}

bool wifi_deauther_start_targeted(const wifi_ap_record_t *ap_record, const uint8_t client_mac[6], deauth_frame_type_t type) {
    uint8_t payload[13];
    memcpy(payload, ap_record->bssid, 6);
    memcpy(payload + 6, client_mac, 6);
    payload[12] = (uint8_t)type;
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_DEAUTHER, payload, 13, NULL, NULL, 2000) == ESP_OK);
}

void wifi_deauther_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
}

bool wifi_deauther_is_running(void) { return true; } // Generic state pull could be added
void wifi_deauther_send_deauth_frame(const wifi_ap_record_t *ap_record, deauth_frame_type_t type) {}
void wifi_deauther_send_broadcast_deauth(const wifi_ap_record_t *ap_record, deauth_frame_type_t type) {}
void wifi_deauther_send_raw_frame(const uint8_t *frame_buffer, int size) {}
void wifi_send_association_request(const wifi_ap_record_t *ap_record) {}
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) { return 0; }
