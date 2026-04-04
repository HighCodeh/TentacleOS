#include "signal_monitor.h"
#include "spi_bridge.h"
#include <string.h>

static int8_t last_rssi = -127;

void signal_monitor_start(const uint8_t *bssid, uint8_t channel) {
    uint8_t payload[7];
    memcpy(payload, bssid, 6);
    payload[6] = channel;
    spi_bridge_send_command(SPI_ID_WIFI_APP_SIGNAL_MON, payload, 7, NULL, NULL, 2000);
}

void signal_monitor_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
}

int8_t signal_monitor_get_rssi(void) {
    spi_header_t resp;
    uint16_t magic_stats = 0xEEEE;
    sniffer_stats_t stats;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_stats, 2, &resp, (uint8_t*)&stats, 1000) == ESP_OK) {
        last_rssi = stats.signal_rssi;
    }
    return last_rssi;
}

uint32_t signal_monitor_get_last_seen_ms(void) { return 0; }
