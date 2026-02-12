#include "wifi_sniffer.h"
#include "spi_bridge.h"
#include <string.h>

static sniffer_stats_t cached_stats;

static void update_stats(void) {
    spi_header_t resp;
    uint16_t magic_stats = 0xEEEE;
    spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_stats, 2, &resp, (uint8_t*)&cached_stats, 1000);
}

bool wifi_sniffer_start(sniff_type_t type, uint8_t channel) {
    uint8_t payload[2];
    payload[0] = (uint8_t)type;
    payload[1] = channel;
    memset(&cached_stats, 0, sizeof(cached_stats));
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_SNIFFER, payload, 2, NULL, NULL, 2000) == ESP_OK);
}

void wifi_sniffer_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
}

uint32_t wifi_sniffer_get_packet_count(void) {
    update_stats();
    return cached_stats.packets;
}

uint32_t wifi_sniffer_get_deauth_count(void) {
    // Stats are updated in packet_count call, usually UI calls these together
    return cached_stats.deauths;
}

uint32_t wifi_sniffer_get_buffer_usage(void) {
    return cached_stats.buffer_usage;
}

bool wifi_sniffer_handshake_captured(void) {
    return cached_stats.handshake_captured;
}

bool wifi_sniffer_pmkid_captured(void) {
    return cached_stats.pmkid_captured;
}

// Stubs for UI flow control
bool wifi_sniffer_save_to_internal_flash(const char *filename) { return true; }
bool wifi_sniffer_save_to_sd_card(const char *filename) { return true; }
void wifi_sniffer_free_buffer(void) {}
void wifi_sniffer_set_snaplen(uint16_t len) {}
void wifi_sniffer_set_verbose(bool verbose) {}
bool wifi_sniffer_start_stream_sd(sniff_type_t type, uint8_t channel, const char *filename) { return true; }
void wifi_sniffer_clear_pmkid(void) {}
void wifi_sniffer_get_pmkid_bssid(uint8_t out_bssid[6]) {}
void wifi_sniffer_clear_handshake(void) {}
void wifi_sniffer_get_handshake_bssid(uint8_t out_bssid[6]) {}
