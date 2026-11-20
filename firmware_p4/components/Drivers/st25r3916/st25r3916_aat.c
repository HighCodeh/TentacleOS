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

#include "st25r3916_aat.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "st25r3916_core.h"
#include "st25r3916_cmd.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"

static const char *TAG = "ST25R3916_AAT";

esp_err_t st25r3916_aat_calibrate(st25r3916_aat_result_t *out_result) {
  if (out_result == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out_result, 0, sizeof(*out_result));
  // AAT_A/AAT_B are unconnected on High Boy V2.03.
  ESP_LOGW(TAG, "Fixed antenna matching; automatic tuning unavailable");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t st25r3916_aat_load_nvs(st25r3916_aat_result_t *out_result) {
  if (out_result == NULL)
    return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t err = nvs_open("hb_aat", NVS_READONLY, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_get_u8(handle, "dac_a", &out_result->dac_a);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }
  err = nvs_get_u8(handle, "dac_b", &out_result->dac_b);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }
  err = nvs_get_u8(handle, "amp", &out_result->amplitude);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }
  err = nvs_get_u8(handle, "phase", &out_result->phase);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  nvs_close(handle);
  return ESP_OK;
}

esp_err_t st25r3916_aat_save_nvs(const st25r3916_aat_result_t *result) {
  if (result == NULL)
    return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t err = nvs_open("hb_aat", NVS_READWRITE, &handle);
  if (err != ESP_OK)
    return err;

  err = nvs_set_u8(handle, "dac_a", result->dac_a);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }
  err = nvs_set_u8(handle, "dac_b", result->dac_b);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }
  err = nvs_set_u8(handle, "amp", result->amplitude);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }
  err = nvs_set_u8(handle, "phase", result->phase);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}
