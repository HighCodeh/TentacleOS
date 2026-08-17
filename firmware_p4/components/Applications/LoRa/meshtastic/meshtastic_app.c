// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "meshtastic_app.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "meshtastic_mesh.h"
#include "meshtastic_nodedb.h"
#include "meshtastic_nvs.h"
#include "meshtastic_phone_bridge.h"
#include "meshtastic_phoneapi.h"
#include "meshtastic_pki.h"
#include "meshtastic_presets.h"
#include "meshtastic_regions.h"
#include "mt_modules.h"
#include "sx1262.h"
#include "sx1262_hal.h"
#include "sx1262_regs.h"

static const char *TAG = "MT_APP";

#define MT_POLL_TASK_STACK      8192
#define MT_POLL_TASK_PRIO       SYS_PRIO_BACKGROUND
#define MT_POLL_PERIOD_MS       50
#define MT_TICK_PERIOD_MS       1000
#define MT_DEFAULT_TX_POWER_DBM 20
#define MT_DEFAULT_PREAMBLE_LEN 16
#define MT_PRIMARY_CHANNEL      19
#define MT_CR_TO_SX1262(cr)     ((uint8_t)((cr) - 4))

static uint32_t derive_node_num(void);
static void poll_task(void *pv);

esp_err_t meshtastic_app_start(void) {
  uint32_t node_num = derive_node_num();
  ESP_LOGI(TAG, "Node num derived: 0x%08lX", (unsigned long)node_num);

  esp_err_t ret = mt_nvs_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "mt_nvs_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  mt_preset_t preset = mt_preset_current();
  const mt_preset_info_t *p_info = mt_preset_info(preset);
  if (p_info == NULL) {
    ESP_LOGE(TAG, "Invalid preset id %d", (int)preset);
    return ESP_FAIL;
  }

  mt_region_t region = mt_region_current();
  uint32_t freq_hz = mt_region_freq_for_channel(region, MT_PRIMARY_CHANNEL, p_info->bw_hz);

  sx1262_deinit();

  sx1262_config_t cfg = {0};
  ret = sx1262_hal_create(&cfg.hal);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sx1262_hal_create failed: %s", esp_err_to_name(ret));
    return ret;
  }

  cfg.frequency_hz = freq_hz;
  cfg.sf = p_info->sf;
  cfg.bw = mt_preset_bw_to_sx1262(p_info->bw_hz);
  cfg.cr = MT_CR_TO_SX1262(p_info->cr);
  cfg.tx_power_dbm = MT_DEFAULT_TX_POWER_DBM;
  cfg.preamble_len = MT_DEFAULT_PREAMBLE_LEN;
  cfg.is_crc_on = true;
  cfg.is_inverted_iq = false;
  cfg.is_implicit_hdr = false;
  cfg.is_public_network = false;

  ret = sx1262_init(&cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sx1262_init failed: %s", esp_err_to_name(ret));
    return ret;
  }
  ret = sx1262_start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sx1262_start failed: %s", esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG,
           "SX1262 ready (%lu Hz, SF%u, BW%lu, CR4/%u, %d dBm) preset=%s",
           (unsigned long)freq_hz,
           p_info->sf,
           (unsigned long)p_info->bw_hz,
           p_info->cr,
           MT_DEFAULT_TX_POWER_DBM,
           p_info->name);

  ret = mt_pki_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "mt_pki_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = mt_nodedb_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "mt_nodedb_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = mt_modules_init(node_num);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "mt_modules_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = meshtastic_mesh_init(node_num);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "meshtastic_mesh_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = phoneapi_init(node_num);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "phoneapi_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = meshtastic_phone_bridge_init(node_num);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "meshtastic_phone_bridge_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = meshtastic_mesh_start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "meshtastic_mesh_start failed: %s", esp_err_to_name(ret));
    return ret;
  }

  if (xTaskCreatePinnedToCore(poll_task,
                              "mt_poll",
                              MT_POLL_TASK_STACK,
                              NULL,
                              MT_POLL_TASK_PRIO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    ESP_LOGE(TAG, "Failed to spawn poll task");
    return ESP_ERR_NO_MEM;
  }

  ret = meshtastic_phone_bridge_ble_start();
  if (ret != ESP_OK) {
    ESP_LOGW(
        TAG, "meshtastic_phone_bridge_ble_start failed: %s (will retry)", esp_err_to_name(ret));
  }

  ESP_LOGI(TAG, "Meshtastic stack online -- waiting for phone");
  return ESP_OK;
}

static uint32_t derive_node_num(void) {
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  return ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) |
         (uint32_t)mac[5];
}

static void poll_task(void *pv) {
  (void)pv;
  const TickType_t poll_period = pdMS_TO_TICKS(MT_POLL_PERIOD_MS);
  const uint32_t ticks_per_module_tick = MT_TICK_PERIOD_MS / MT_POLL_PERIOD_MS;
  uint32_t tick_counter = 0;
  while (1) {
    meshtastic_mesh_poll();
    if (++tick_counter >= ticks_per_module_tick) {
      tick_counter = 0;
      mt_modules_tick();
    }
    vTaskDelay(poll_period);
  }
}
