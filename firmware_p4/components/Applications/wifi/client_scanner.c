#include "client_scanner.h"
#include "spi_bridge.h"
#include <string.h>
#include <stdlib.h>

static client_record_t cached_client;
static client_record_t *cached_results = NULL;
static uint16_t cached_count = 0;
static bool scan_ready = false;
static client_record_t empty_record;

static bool client_scanner_fetch_results(void) {
    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = SPI_DATA_INDEX_COUNT;

    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) != ESP_OK) {
        return false;
    }

    uint16_t count = 0;
    memcpy(&count, payload, 2);

    if (cached_results) {
        free(cached_results);
        cached_results = NULL;
    }
    cached_count = 0;

    if (count == 0) {
        scan_ready = true;
        return true;
    }

    cached_results = (client_record_t *)malloc(count * sizeof(client_record_t));
    if (!cached_results) {
        return false;
    }

    for (uint16_t i = 0; i < count; i++) {
        if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&i, 2, &resp, (uint8_t*)&cached_results[i], 1000) != ESP_OK) {
            free(cached_results);
            cached_results = NULL;
            return false;
        }
    }

    cached_count = count;
    scan_ready = true;
    return true;
}

bool client_scanner_start(void) {
    client_scanner_free_results();
    esp_err_t err = spi_bridge_send_command(SPI_ID_WIFI_APP_SCAN_CLIENT, NULL, 0, NULL, NULL, 20000);
    if (err != ESP_OK) return false;
    return client_scanner_fetch_results();
}

client_record_t* client_scanner_get_results(uint16_t *count) {
    if (!scan_ready) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = cached_count;
    return cached_results ? cached_results : &empty_record;
}

// Added for SPI bridging compatibility:
client_record_t* client_scanner_get_result_by_index(uint16_t index) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, (uint8_t*)&cached_client, 1000) == ESP_OK) {
        return &cached_client;
    }
    return NULL;
}

void client_scanner_free_results(void) {
    if (cached_results) {
        free(cached_results);
        cached_results = NULL;
    }
    cached_count = 0;
    scan_ready = false;
}
bool client_scanner_save_results_to_internal_flash(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_CLIENT_SAVE_FLASH, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}
bool client_scanner_save_results_to_sd_card(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_CLIENT_SAVE_SD, NULL, 0, NULL, NULL, 5000) == ESP_OK);
}
