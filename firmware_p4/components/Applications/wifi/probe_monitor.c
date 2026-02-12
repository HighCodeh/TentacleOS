#include "probe_monitor.h"
#include "spi_bridge.h"
#include <string.h>

static probe_record_t cached_probe;

bool probe_monitor_start(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_PROBE_MON, NULL, 0, NULL, NULL, 2000) == ESP_OK);
}

void probe_monitor_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
}

probe_record_t* probe_monitor_get_results(uint16_t *count) {
    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = 0xFFFF;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) == ESP_OK) {
        memcpy(count, payload, 2);
    }
    return NULL;
}

probe_record_t* probe_monitor_get_result_by_index(uint16_t index) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, (uint8_t*)&cached_probe, 1000) == ESP_OK) {
        return &cached_probe;
    }
    return NULL;
}

void probe_monitor_free_results(void) {}
bool probe_monitor_save_results_to_internal_flash(void) { return true; }
bool probe_monitor_save_results_to_sd_card(void) { return true; }
