#include "bridge_manager.h"
#include "spi_bridge.h"
#include "c5_flasher.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BRIDGE_MGR";
static const char *EMBEDDED_VERSION = "1.0.0";

esp_err_t bridge_manager_init(void) {
    ESP_LOGI(TAG, "Initializing Bridge Manager...");

    // 1. Init SPI Master
    if (spi_bridge_master_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SPI bridge.");
        return ESP_FAIL;
    }

    // 2. Query C5 Version
    spi_header_t resp_header;
    uint8_t resp_ver[32];
    memset(resp_ver, 0, sizeof(resp_ver));

    ESP_LOGI(TAG, "Checking C5 version...");
    esp_err_t ret = spi_bridge_send_command(SPI_ID_SYSTEM_VERSION, NULL, 0, &resp_header, resp_ver, 1000);

    bool needs_update = false;

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "C5 not responding. Assuming recovery needed.");
        needs_update = true;
    } else {
        ESP_LOGI(TAG, "C5 Version: %s (Embedded: %s)", resp_ver, EMBEDDED_VERSION);
        if (strcmp((char*)resp_ver, EMBEDDED_VERSION) != 0) {
            needs_update = true;
        }
    }

    // 3. Update if needed
    if (needs_update) {
        ESP_LOGW(TAG, "C5 update required!");
        c5_flasher_init();
        if (c5_flasher_update(NULL, 0) == ESP_OK) {
            ESP_LOGI(TAG, "C5 synchronized successfully.");
        } else {
            ESP_LOGE(TAG, "C5 synchronization failed.");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGI(TAG, "C5 is up to date.");
    }

    return ESP_OK;
}

esp_err_t bridge_manager_force_update(void) {
    c5_flasher_init();
    return c5_flasher_update(NULL, 0);
}
