#include "probe_monitor.h"
#include "spi_bridge.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static probe_record_t cached_probe;
static probe_record_t *cached_results = NULL;
static uint16_t cached_count = 0;
static uint16_t cached_capacity = 0;
static probe_record_t empty_record;

#define PROBE_MONITOR_MAX_RESULTS 300

bool probe_monitor_start(void) {
    probe_monitor_free_results();
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_PROBE_MON, NULL, 0, NULL, NULL, 2000) == ESP_OK);
}

void probe_monitor_stop(void) {
    spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
    probe_monitor_free_results();
}

probe_record_t* probe_monitor_get_results(uint16_t *count) {
    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = SPI_DATA_INDEX_COUNT;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) != ESP_OK) {
        if (count) *count = cached_count;
        return cached_results ? cached_results : &empty_record;
    }

    uint16_t remote_count = 0;
    memcpy(&remote_count, payload, 2);
    if (remote_count > PROBE_MONITOR_MAX_RESULTS) {
        remote_count = PROBE_MONITOR_MAX_RESULTS;
    }

    if (remote_count < cached_count) {
        cached_count = 0;
    }

    if (remote_count > cached_capacity) {
        probe_record_t *new_buf = (probe_record_t *)realloc(cached_results, remote_count * sizeof(probe_record_t));
        if (!new_buf) {
            if (count) *count = cached_count;
            return cached_results ? cached_results : &empty_record;
        }
        cached_results = new_buf;
        cached_capacity = remote_count;
    }

    uint16_t fetched = cached_count;
    for (uint16_t i = cached_count; i < remote_count; i++) {
        if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&i, 2, &resp, (uint8_t*)&cached_results[i], 1000) != ESP_OK) {
            break;
        }
        fetched = i + 1;
    }

    cached_count = fetched;
    if (count) *count = cached_count;
    return cached_results ? cached_results : &empty_record;
}

probe_record_t* probe_monitor_get_result_by_index(uint16_t index) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, (uint8_t*)&cached_probe, 1000) == ESP_OK) {
        return &cached_probe;
    }
    return NULL;
}

void probe_monitor_free_results(void) {
    if (cached_results) {
        free(cached_results);
        cached_results = NULL;
    }
    cached_count = 0;
    cached_capacity = 0;
}
bool probe_monitor_save_results_to_internal_flash(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_PROBE_SAVE_FLASH, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}
bool probe_monitor_save_results_to_sd_card(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_PROBE_SAVE_SD, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}
