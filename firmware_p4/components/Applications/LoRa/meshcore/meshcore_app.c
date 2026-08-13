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

#include "meshcore_app.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "meshcore.h"
#include "meshcore_nvs.h"
#include "meshcore_phone_bridge.h"
#include "meshcore_phoneapi.h"
#include "sodium.h"
#include "sx1262.h"
#include "sx1262_hal.h"
#include "sx1262_regs.h"

static const char *TAG = "MC_APP";

#define MC_IDENTITY_NVS_KEY   "id_seed"
#define MC_IDENTITY_SEED_SIZE 32
#define MC_DEFAULT_NAME       "Highboy"
#define MC_BRIDGE_NAME_PREFIX NULL

#define MC_POLL_TASK_STACK 12288
#define MC_POLL_TASK_PRIO SYS_PRIO_BACKGROUND
#define MC_POLL_PERIOD_MS  50

static void poll_task(void *pv);
static void
on_advert_cb(const meshcore_contact_t *contact, int16_t rssi_dbm, int8_t snr_db, void *ctx);
static void on_grp_txt_cb(uint8_t channel_idx,
                          uint8_t path_len,
                          const char *text,
                          uint32_t timestamp,
                          int16_t rssi_dbm,
                          int8_t snr_db,
                          void *ctx);
static void on_direct_msg_cb(const uint8_t peer_pub_key[32],
                             const char *text,
                             uint32_t timestamp,
                             uint8_t txt_type,
                             uint8_t path_len,
                             int16_t rssi_dbm,
                             int8_t snr_db,
                             void *ctx);
static void on_ack_cb(uint32_t ack_crc, const uint8_t peer_pub_key[32], void *ctx);
static void on_path_update_cb(const meshcore_contact_t *contact, void *ctx);
static esp_err_t load_or_create_identity(meshcore_identity_t *out);

esp_err_t meshcore_app_start(void) {
  if (sodium_init() < 0) {
    ESP_LOGE(TAG, "sodium_init failed");
    return ESP_FAIL;
  }

  sx1262_config_t cfg = {0};
  esp_err_t ret = sx1262_hal_create(&cfg.hal);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sx1262_hal_create failed: %s", esp_err_to_name(ret));
    return ret;
  }

  cfg.frequency_hz = MESHCORE_FREQ_HZ;
  cfg.sf = MESHCORE_SF;
  cfg.bw = SX1262_LORA_BW_250;
  cfg.cr = SX1262_LORA_CR_4_5;
  cfg.tx_power_dbm = MESHCORE_TX_POWER_DBM;
  cfg.preamble_len = MESHCORE_PREAMBLE_LEN;
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
           "SX1262 ready (%lu Hz, SF%u, BW250k, CR4/5, %d dBm)",
           (unsigned long)MESHCORE_FREQ_HZ,
           MESHCORE_SF,
           MESHCORE_TX_POWER_DBM);

  meshcore_identity_t identity;
  ret = load_or_create_identity(&identity);
  if (ret != ESP_OK) {
    return ret;
  }

  meshcore_callbacks_t cbs = {
      .on_advert = on_advert_cb,
      .on_grp_txt = on_grp_txt_cb,
      .on_direct_msg = on_direct_msg_cb,
      .on_ack = on_ack_cb,
      .on_path_update = on_path_update_cb,
      .ctx = NULL,
  };
  ret = meshcore_init(&identity, &cbs);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "meshcore_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = meshcore_phoneapi_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "meshcore_phoneapi_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = meshcore_phone_bridge_init(MC_BRIDGE_NAME_PREFIX);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "meshcore_phone_bridge_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = meshcore_start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "meshcore_start failed: %s", esp_err_to_name(ret));
    return ret;
  }

  if (xTaskCreatePinnedToCore(poll_task, "mc_poll", MC_POLL_TASK_STACK, NULL, MC_POLL_TASK_PRIO, NULL, SYS_CORE_RADIO) !=
      pdPASS) {
    ESP_LOGE(TAG, "Failed to spawn poll task");
    return ESP_ERR_NO_MEM;
  }

  ret = meshcore_phone_bridge_ble_start();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "meshcore_phone_bridge_ble_start failed: %s (will retry)", esp_err_to_name(ret));
  }

  ESP_LOGI(TAG, "MeshCore stack online -- waiting for phone");
  return ESP_OK;
}

static void poll_task(void *pv) {
  (void)pv;
  const TickType_t period = pdMS_TO_TICKS(MC_POLL_PERIOD_MS);
  while (1) {
    meshcore_poll();
    vTaskDelay(period);
  }
}

static void
on_advert_cb(const meshcore_contact_t *contact, int16_t rssi_dbm, int8_t snr_db, void *ctx) {
  (void)rssi_dbm;
  (void)snr_db;
  (void)ctx;
  meshcore_phoneapi_push_new_advert(contact);
}

static void on_grp_txt_cb(uint8_t channel_idx,
                          uint8_t path_len,
                          const char *text,
                          uint32_t timestamp,
                          int16_t rssi_dbm,
                          int8_t snr_db,
                          void *ctx) {
  (void)rssi_dbm;
  (void)ctx;
  meshcore_phoneapi_push_channel_msg(channel_idx, path_len, timestamp, snr_db, text);
}

static void on_direct_msg_cb(const uint8_t peer_pub_key[32],
                             const char *text,
                             uint32_t timestamp,
                             uint8_t txt_type,
                             uint8_t path_len,
                             int16_t rssi_dbm,
                             int8_t snr_db,
                             void *ctx) {
  (void)rssi_dbm;
  (void)ctx;
  meshcore_phoneapi_push_contact_msg(peer_pub_key, path_len, txt_type, timestamp, snr_db, text);
}

static void on_ack_cb(uint32_t ack_crc, const uint8_t peer_pub_key[32], void *ctx) {
  (void)peer_pub_key;
  (void)ctx;
  meshcore_phoneapi_push_send_confirmed(ack_crc, 0);
}

static void on_path_update_cb(const meshcore_contact_t *contact, void *ctx) {
  (void)ctx;
  meshcore_phoneapi_push_path_updated(contact);
}

static esp_err_t load_or_create_identity(meshcore_identity_t *out) {
  memset(out, 0, sizeof(*out));

  uint8_t seed[MC_IDENTITY_SEED_SIZE];
  size_t len = sizeof(seed);
  esp_err_t ret = mc_nvs_get_blob(MC_IDENTITY_NVS_KEY, seed, &len);
  bool is_loaded = (ret == ESP_OK && len == sizeof(seed));

  if (!is_loaded) {
    esp_fill_random(seed, sizeof(seed));
    ret = mc_nvs_set_blob(MC_IDENTITY_NVS_KEY, seed, sizeof(seed));
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Persist identity seed failed: %s", esp_err_to_name(ret));
      sodium_memzero(seed, sizeof(seed));
      return ret;
    }
  }

  if (crypto_sign_seed_keypair(out->pub_key, out->priv_key, seed) != 0) {
    ESP_LOGE(TAG, "crypto_sign_seed_keypair failed");
    sodium_memzero(seed, sizeof(seed));
    return ESP_FAIL;
  }
  sodium_memzero(seed, sizeof(seed));

  ESP_LOGI(TAG,
           "Identity %s (pub %02X%02X%02X%02X..)",
           is_loaded ? "loaded from NVS" : "generated",
           out->pub_key[0],
           out->pub_key[1],
           out->pub_key[2],
           out->pub_key[3]);

  strncpy(out->name, MC_DEFAULT_NAME, MESHCORE_NAME_MAX - 1);
  out->name[MESHCORE_NAME_MAX - 1] = 0;
  out->adv_type = MC_ADV_TYPE_CHAT;
  return ESP_OK;
}
