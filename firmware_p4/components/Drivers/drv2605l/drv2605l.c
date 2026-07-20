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

#include "drv2605l.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_def.h"

static const char *TAG = "DRV2605L";

#define REG_STATUS        0x00
#define REG_MODE          0x01
#define REG_RTP_INPUT     0x02
#define REG_LIB_SEL       0x03
#define REG_WAVESEQ_0     0x04
#define REG_GO            0x0C
#define REG_RATED_V       0x16
#define REG_OD_CLAMP      0x17
#define REG_A_CAL_COMP    0x18
#define REG_A_CAL_BEMF    0x19
#define REG_FEEDBACK_CTRL 0x1A
#define REG_CONTROL1      0x1B
#define REG_CONTROL2      0x1C
#define REG_CONTROL3      0x1D
#define REG_DEVICE_ID     0x00

#define MODE_INTERNAL_TRIG 0x00
#define MODE_AUTO_CAL      0x07
#define MODE_RTP           0x05
#define LIB_TS2200_A       0x01

#define RATED_V_ERM  0x90
#define OD_CLAMP_ERM 0xCC

#define I2C_TIMEOUT pdMS_TO_TICKS(50)

static uint8_t s_device_id = 0;
static bool s_ready = false;
static bool s_rtp_mode = false;
static uint8_t s_last_effect = 0xFF;

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  return i2c_master_write_to_device(I2C_NUM_0, DRV2605L_I2C_ADDR, buf, sizeof(buf), I2C_TIMEOUT);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *out_val) {
  return i2c_master_write_read_device(
      I2C_NUM_0, DRV2605L_I2C_ADDR, &reg, 1, out_val, 1, I2C_TIMEOUT);
}

uint8_t drv2605l_device_id(void) {
  return s_device_id;
}

esp_err_t drv2605l_init(void) {
  vTaskDelay(pdMS_TO_TICKS(2));

  uint8_t status = 0;
  esp_err_t ret = read_reg(REG_STATUS, &status);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Status read failed: %s", esp_err_to_name(ret));
    return ret;
  }
  s_device_id = (status >> 5) & 0x07;

  ret = write_reg(REG_MODE, MODE_INTERNAL_TRIG);
  if (ret == ESP_OK)
    ret = write_reg(REG_LIB_SEL, LIB_TS2200_A);

  if (ret == ESP_OK)
    ret = write_reg(REG_RATED_V, RATED_V_ERM);
  if (ret == ESP_OK)
    ret = write_reg(REG_OD_CLAMP, OD_CLAMP_ERM);

  if (ret == ESP_OK)
    ret = write_reg(REG_FEEDBACK_CTRL, 0x35);
  if (ret == ESP_OK)
    ret = write_reg(REG_CONTROL1, 0x93);
  if (ret == ESP_OK)
    ret = write_reg(REG_CONTROL2, 0xF5);
  if (ret == ESP_OK)
    ret = write_reg(REG_CONTROL3, 0xA0);

  if (ret != ESP_OK) {
    return ret;
  }

  s_ready = true;
  ESP_LOGI(TAG,
           "DRV2605L ready (DEVICE_ID=%u, rated=0x%02X clamp=0x%02X)",
           s_device_id,
           RATED_V_ERM,
           OD_CLAMP_ERM);
  return ESP_OK;
}

esp_err_t drv2605l_play_effect(uint8_t effect) {
  if (!s_ready) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_rtp_mode) {
    esp_err_t mret = write_reg(REG_MODE, MODE_INTERNAL_TRIG);
    if (mret != ESP_OK)
      return mret;
    s_rtp_mode = false;
    s_last_effect = 0xFF;
  }
  if (effect != s_last_effect) {
    esp_err_t ret = write_reg(REG_WAVESEQ_0, effect);
    if (ret == ESP_OK)
      ret = write_reg(REG_WAVESEQ_0 + 1, 0);
    if (ret != ESP_OK) {
      return ret;
    }
    s_last_effect = effect;
  }
  return write_reg(REG_GO, 0x01);
}

esp_err_t drv2605l_stop(void) {
  if (!s_ready) {
    return ESP_ERR_INVALID_STATE;
  }
  return write_reg(REG_GO, 0x00);
}

esp_err_t drv2605l_autocal(void) {
  if (!s_ready) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = write_reg(REG_MODE, MODE_AUTO_CAL);
  if (ret == ESP_OK)
    ret = write_reg(REG_GO, 0x01);
  if (ret != ESP_OK) {
    return ret;
  }
  uint8_t go = 1;
  for (int i = 0; i < 150 && (go & 0x01); i++) {
    vTaskDelay(pdMS_TO_TICKS(10));
    if (read_reg(REG_GO, &go) != ESP_OK)
      break;
  }
  uint8_t status = 0;
  read_reg(REG_STATUS, &status);
  bool ok = ((status & 0x08) == 0);

  write_reg(REG_MODE, MODE_INTERNAL_TRIG);
  s_rtp_mode = false;
  s_last_effect = 0xFF;
  ESP_LOGI(TAG, "auto-cal %s (status=0x%02X)", ok ? "PASS" : "FAIL", status);
  return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t drv2605l_set_rtp(uint8_t intensity) {
  if (!s_ready) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = write_reg(REG_MODE, MODE_RTP);
  if (ret == ESP_OK) {
    s_rtp_mode = true;
    ret = write_reg(REG_RTP_INPUT, intensity);
  }
  return ret;
}
