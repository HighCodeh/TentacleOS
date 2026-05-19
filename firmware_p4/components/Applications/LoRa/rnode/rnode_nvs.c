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

#include "rnode_internal.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "RNODE_NVS";

#define RNODE_NVS_NAMESPACE "rnode"
#define RNODE_NVS_KEY_CFG   "radio_cfg"

#define RNODE_FREQ_MIN_HZ      137000000UL
#define RNODE_FREQ_MAX_HZ      3000000000UL
#define RNODE_BW_MIN_HZ        7800UL
#define RNODE_BW_MAX_HZ        1625000UL
#define RNODE_SF_MIN           5
#define RNODE_SF_MAX           12
#define RNODE_CR_MIN           5
#define RNODE_CR_MAX           8
#define RNODE_TX_POWER_MIN_DBM (-9)
#define RNODE_TX_POWER_MAX_DBM 22

void rnode_nvs_save_cfg(void) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(RNODE_NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    return;
  }
  nvs_set_blob(h, RNODE_NVS_KEY_CFG, rnode_cfg_mut(), sizeof(rnode_radio_cfg_t));
  nvs_commit(h);
  nvs_close(h);
}

void rnode_nvs_load_cfg(void) {
  rnode_radio_cfg_t *cfg = rnode_cfg_mut();
  cfg->freq_hz = RNODE_DEFAULT_FREQ_HZ;
  cfg->bw_hz = RNODE_DEFAULT_BW_HZ;
  cfg->sf = RNODE_DEFAULT_SF;
  cfg->cr = RNODE_DEFAULT_CR;
  cfg->tx_power_dbm = RNODE_DEFAULT_TX_POWER_DBM;
  cfg->is_radio_on = false;

  nvs_handle_t h;
  esp_err_t err = nvs_open(RNODE_NVS_NAMESPACE, NVS_READONLY, &h);
  if (err != ESP_OK) {
    return;
  }

  rnode_radio_cfg_t persisted = {0};
  size_t sz = sizeof(persisted);
  err = nvs_get_blob(h, RNODE_NVS_KEY_CFG, &persisted, &sz);
  nvs_close(h);

  if (err != ESP_OK || sz != sizeof(persisted)) {
    return;
  }
  if (persisted.freq_hz >= RNODE_FREQ_MIN_HZ && persisted.freq_hz <= RNODE_FREQ_MAX_HZ) {
    cfg->freq_hz = persisted.freq_hz;
  }
  if (persisted.bw_hz >= RNODE_BW_MIN_HZ && persisted.bw_hz <= RNODE_BW_MAX_HZ) {
    cfg->bw_hz = persisted.bw_hz;
  }
  if (persisted.sf >= RNODE_SF_MIN && persisted.sf <= RNODE_SF_MAX) {
    cfg->sf = persisted.sf;
  }
  if (persisted.cr >= RNODE_CR_MIN && persisted.cr <= RNODE_CR_MAX) {
    cfg->cr = persisted.cr;
  }
  if (persisted.tx_power_dbm >= RNODE_TX_POWER_MIN_DBM &&
      persisted.tx_power_dbm <= RNODE_TX_POWER_MAX_DBM) {
    cfg->tx_power_dbm = persisted.tx_power_dbm;
  }
  ESP_LOGI(TAG, "Loaded radio cfg from NVS");
}
