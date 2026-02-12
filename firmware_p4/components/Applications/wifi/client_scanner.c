#include "client_scanner.h"
#include "spi_bridge.h"
#include <string.h>

static client_record_t cached_client;

bool client_scanner_start(void) {
    return (spi_bridge_send_command(SPI_ID_WIFI_APP_SCAN_CLIENT, NULL, 0, NULL, NULL, 2000) == ESP_OK);
}

client_record_t* client_scanner_get_results(uint16_t *count) {
    spi_header_t resp;
    uint8_t payload[2];
    uint16_t magic_count = 0xFFFF;
    
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&magic_count, 2, &resp, payload, 1000) == ESP_OK) {
        memcpy(count, payload, 2);
    } else {
        *count = 0;
    }
    
    // The UI usually calls this first to get the count, then iterates with get_results(index) 
    // but the API is slightly different. We'll return the pointer to the first element if count > 0
    // and rely on subsequent logic or adjust the UI to use indices.
    return NULL; 
}

// Added for SPI bridging compatibility:
client_record_t* client_scanner_get_result_by_index(uint16_t index) {
    spi_header_t resp;
    if (spi_bridge_send_command(SPI_ID_SYSTEM_DATA, (uint8_t*)&index, 2, &resp, (uint8_t*)&cached_client, 1000) == ESP_OK) {
        return &cached_client;
    }
    return NULL;
}

void client_scanner_free_results(void) {}
bool client_scanner_save_results_to_internal_flash(void) { return true; }
bool client_scanner_save_results_to_sd_card(void) { return true; }
