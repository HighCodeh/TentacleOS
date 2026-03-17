#include "bridge_manager.h"
#include "spi_bridge.h"
#include "c5_flasher.h"
#include "storage_assets.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "BRIDGE_MGR";

#define VERSION_JSON_PATH "config/OTA/firmware.json"

static char s_expected_version[32] = "unknown";

static void load_expected_version(void) {
  size_t size;
  uint8_t *json_data = storage_assets_load_file(VERSION_JSON_PATH, &size);
  if (json_data == NULL) {
    ESP_LOGW(TAG, "Could not read firmware.json, using fallback version");
    return;
  }

  cJSON *root = cJSON_ParseWithLength((const char *)json_data, size);
  free(json_data);

  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to parse firmware.json");
    return;
  }

  cJSON *version = cJSON_GetObjectItem(root, "version");
  if (cJSON_IsString(version) && version->valuestring != NULL) {
    strncpy(s_expected_version, version->valuestring, sizeof(s_expected_version) - 1);
    s_expected_version[sizeof(s_expected_version) - 1] = '\0';
  }

  cJSON_Delete(root);
}

esp_err_t bridge_manager_init(void) {
  ESP_LOGI(TAG, "Initializing Bridge Manager...");

  load_expected_version();
  ESP_LOGI(TAG, "Expected C5 version: %s", s_expected_version);

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
    ESP_LOGI(TAG, "C5 Version: %s (Expected: %s)", resp_ver, s_expected_version);
    if (strcmp((char*)resp_ver, s_expected_version) != 0) {
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
