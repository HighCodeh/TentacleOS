// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "meshcore_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "meshcore_nvs.h"
#include "sx1262.h"
#include "sx1262_regs.h"

static const char *TAG = "MESHCORE";

#define MC_NVS_KEY_NAME       "node_name"
#define MC_NVS_KEY_LAT        "adv_lat"
#define MC_NVS_KEY_LON        "adv_lon"
#define MC_NVS_KEY_HAS_LATLON "has_latlon"
#define MC_NVS_KEY_RADIO      "radio"

static meshcore_identity_t s_identity;
static meshcore_callbacks_t s_cbs;
static bool s_is_running = false;

static int32_t s_adv_lat = 0;
static int32_t s_adv_lon = 0;
static bool s_has_latlon = false;

static meshcore_radio_prefs_t s_radio_prefs;

static uint32_t s_time_base_unix = 0;
static uint32_t s_time_base_tick_ms = 0;

esp_err_t meshcore_init(const meshcore_identity_t *identity, const meshcore_callbacks_t *cbs) {
  if (identity == NULL)
    return ESP_ERR_INVALID_ARG;

  mc_nvs_init();

  memcpy(&s_identity, identity, sizeof(*identity));

  char name_nvs[MESHCORE_NAME_MAX] = {0};
  if (mc_nvs_get_str(MC_NVS_KEY_NAME, name_nvs, sizeof(name_nvs)) == ESP_OK && name_nvs[0] != 0) {
    strncpy(s_identity.name, name_nvs, MESHCORE_NAME_MAX - 1);
    s_identity.name[MESHCORE_NAME_MAX - 1] = 0;
  }

  s_has_latlon = false;
  uint32_t flag_v;
  if (mc_nvs_get_u32(MC_NVS_KEY_HAS_LATLON, &flag_v) == ESP_OK && flag_v != 0) {
    uint32_t lat_u, lon_u;
    if (mc_nvs_get_u32(MC_NVS_KEY_LAT, &lat_u) == ESP_OK &&
        mc_nvs_get_u32(MC_NVS_KEY_LON, &lon_u) == ESP_OK) {
      s_adv_lat = (int32_t)lat_u;
      s_adv_lon = (int32_t)lon_u;
      s_has_latlon = true;
    }
  }

  s_radio_prefs.freq_hz = MESHCORE_FREQ_HZ;
  s_radio_prefs.bw_hz = MESHCORE_BW_HZ;
  s_radio_prefs.sf = MESHCORE_SF;
  s_radio_prefs.cr = MESHCORE_CR;
  s_radio_prefs.tx_power_dbm = MESHCORE_TX_POWER_DBM;
  size_t rlen = sizeof(s_radio_prefs);
  mc_nvs_get_blob(MC_NVS_KEY_RADIO, &s_radio_prefs, &rlen);

  if (cbs == NULL) {
    memset(&s_cbs, 0, sizeof(s_cbs));
  } else {
    s_cbs = *cbs;
  }

  esp_err_t db_error = mc_db_init();
  if (db_error != ESP_OK)
    return db_error;
  mc_router_init();

  ESP_LOGI(TAG,
           "Init: node='%s' adv_type=%u (latlon=%d %d %s)",
           s_identity.name,
           s_identity.adv_type,
           (int)s_adv_lat,
           (int)s_adv_lon,
           s_has_latlon ? "yes" : "no");
  return ESP_OK;
}

esp_err_t meshcore_start(void) {
  if (s_is_running)
    return ESP_ERR_INVALID_STATE;

  esp_err_t ret = sx1262_receive_continuous();
  if (ret != ESP_OK)
    return ret;

  s_is_running = true;
  ESP_LOGI(TAG, "Started -- RX continuous");
  return ESP_OK;
}

void meshcore_stop(void) {
  if (!s_is_running)
    return;
  sx1262_stop_rx();
  s_is_running = false;
}

void meshcore_poll(void) {
  if (s_is_running)
    sx1262_process_irq();
  mc_pendings_gc();
  mc_router_relay_process();
}

void meshcore_set_node_name(const char *name) {
  if (name == NULL)
    return;
  strncpy(s_identity.name, name, MESHCORE_NAME_MAX - 1);
  s_identity.name[MESHCORE_NAME_MAX - 1] = 0;
  mc_nvs_set_str(MC_NVS_KEY_NAME, s_identity.name);
  ESP_LOGI(TAG, "Node name set to '%s'", s_identity.name);
  meshcore_send_advert_throttled();
}

void meshcore_set_advert_latlon(int32_t lat_e6, int32_t lon_e6, bool has_latlon) {
  s_adv_lat = lat_e6;
  s_adv_lon = lon_e6;
  s_has_latlon = has_latlon;
  mc_nvs_set_u32(MC_NVS_KEY_HAS_LATLON, has_latlon ? 1 : 0);
  if (has_latlon) {
    mc_nvs_set_u32(MC_NVS_KEY_LAT, (uint32_t)lat_e6);
    mc_nvs_set_u32(MC_NVS_KEY_LON, (uint32_t)lon_e6);
  }
  ESP_LOGI(TAG, "Adv latlon: %d %d (%s)", (int)lat_e6, (int)lon_e6, has_latlon ? "on" : "off");
  meshcore_send_advert_throttled();
}

void meshcore_get_pub_key(uint8_t out[32]) {
  if (out != NULL)
    memcpy(out, s_identity.pub_key, 32);
}

void meshcore_get_name(char *out, size_t max) {
  if (out == NULL || max == 0)
    return;
  strncpy(out, s_identity.name, max - 1);
  out[max - 1] = 0;
}

void meshcore_get_advert_latlon(int32_t *out_lat_e6, int32_t *out_lon_e6, bool *out_has) {
  if (out_lat_e6 != NULL)
    *out_lat_e6 = s_adv_lat;
  if (out_lon_e6 != NULL)
    *out_lon_e6 = s_adv_lon;
  if (out_has != NULL)
    *out_has = s_has_latlon;
}

uint8_t meshcore_get_adv_type(void) {
  return s_identity.adv_type;
}

void meshcore_set_unix_time(uint32_t ts) {
  s_time_base_unix = ts;
  s_time_base_tick_ms = mc_now_ms();
  ESP_LOGI(TAG, "Unix time synced: %lu", (unsigned long)ts);
}

uint32_t meshcore_get_unix_time(void) {
  if (s_time_base_unix == 0) {
    return MC_FALLBACK_EPOCH + (mc_now_ms() / 1000);
  }
  return s_time_base_unix + ((mc_now_ms() - s_time_base_tick_ms) / 1000);
}

esp_err_t meshcore_set_radio_params(
    uint32_t freq_hz, uint32_t bw_hz, uint8_t sf, uint8_t cr, int8_t tx_power_dbm) {
  if (freq_hz < 150000000UL || freq_hz > 960000000UL)
    return ESP_ERR_INVALID_ARG;
  if (sf < 5 || sf > 12)
    return ESP_ERR_INVALID_ARG;
  if (cr < 5 || cr > 8)
    return ESP_ERR_INVALID_ARG;
  if (tx_power_dbm < -9 || tx_power_dbm > 22)
    return ESP_ERR_INVALID_ARG;

  static const struct {
    uint32_t hz;
    uint8_t code;
  } BW_TABLE[] = {
      {7810, SX1262_LORA_BW_7},
      {10420, SX1262_LORA_BW_10},
      {15630, SX1262_LORA_BW_15},
      {20830, SX1262_LORA_BW_20},
      {31250, SX1262_LORA_BW_31},
      {41670, SX1262_LORA_BW_41},
      {62500, SX1262_LORA_BW_62},
      {125000, SX1262_LORA_BW_125},
      {250000, SX1262_LORA_BW_250},
      {500000, SX1262_LORA_BW_500},
  };
#define BW_TABLE_COUNT (sizeof(BW_TABLE) / sizeof(BW_TABLE[0]))

  uint8_t bw_code = 0xFF;
  uint32_t best_diff = UINT32_MAX;
  for (size_t i = 0; i < BW_TABLE_COUNT; i++) {
    uint32_t d = (bw_hz > BW_TABLE[i].hz) ? (bw_hz - BW_TABLE[i].hz) : (BW_TABLE[i].hz - bw_hz);
    if (d < best_diff) {
      best_diff = d;
      bw_code = BW_TABLE[i].code;
    }
  }
  if (best_diff * 10 > bw_hz)
    return ESP_ERR_INVALID_ARG;

  s_radio_prefs.freq_hz = freq_hz;
  s_radio_prefs.bw_hz = bw_hz;
  s_radio_prefs.sf = sf;
  s_radio_prefs.cr = cr;
  s_radio_prefs.tx_power_dbm = tx_power_dbm;
  mc_nvs_set_blob(MC_NVS_KEY_RADIO, &s_radio_prefs, sizeof(s_radio_prefs));

  sx1262_stop_rx();
  sx1262_config_t cfg = {0};
  cfg.frequency_hz = freq_hz;
  cfg.sf = sf;
  cfg.bw = bw_code;
  cfg.cr = (cr - 4);
  cfg.tx_power_dbm = tx_power_dbm;
  cfg.preamble_len = MESHCORE_PREAMBLE_LEN;
  cfg.is_crc_on = true;
  cfg.is_inverted_iq = false;
  cfg.is_implicit_hdr = false;
  cfg.is_public_network = false;
  esp_err_t ret = sx1262_config_lora(&cfg);
  if (s_is_running)
    sx1262_receive_continuous();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG,
             "Radio reconfigured: %luHz BW%lu SF%u CR4/%u TX%ddBm",
             (unsigned long)freq_hz,
             (unsigned long)bw_hz,
             sf,
             cr,
             tx_power_dbm);
  } else {
    ESP_LOGE(TAG, "Radio reconfig failed: %s", esp_err_to_name(ret));
  }
  return ret;
}

const meshcore_radio_prefs_t *meshcore_get_radio_prefs(void) {
  return &s_radio_prefs;
}

const meshcore_identity_t *mc_get_identity(void) {
  return &s_identity;
}

const meshcore_callbacks_t *mc_get_callbacks(void) {
  return &s_cbs;
}

bool mc_is_running(void) {
  return s_is_running;
}

void mc_get_advert_latlon_internal(int32_t *out_lat, int32_t *out_lon, bool *out_has) {
  if (out_lat != NULL)
    *out_lat = s_adv_lat;
  if (out_lon != NULL)
    *out_lon = s_adv_lon;
  if (out_has != NULL)
    *out_has = s_has_latlon;
}

uint32_t mc_now_ms(void) {
  return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
