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

#include "bq25896.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "pin_def.h"

#define I2C_PORT       I2C_NUM_0
#define I2C_TIMEOUT_MS 100
#define BATV_BASE_MV   2304
#define BATV_STEP_MV   20
#define BATTERY_MIN_MV 3200
#define BATTERY_MAX_MV 4200

// Definições dos Registradores
#define REG_ILIM       0x00
#define REG_VINDPM     0x01
#define REG_ADC_CTRL   0x02
#define REG_CHG_CTRL_0 0x03
#define REG_ICHG       0x04
#define REG_IPRE_ITERM 0x05
#define REG_VREG       0x06
#define REG_CHG_CTRL_1 0x07
#define REG_CHG_TIMER  0x08
#define REG_BAT_COMP   0x09
#define REG_CHG_CTRL_2 0x0A
#define REG_STATUS     0x0B
#define REG_FAULT      0x0C
#define REG_VINDPM_OS  0x0D
#define REG_BAT_VOLT   0x0E
#define REG_SYS_VOLT   0x0F
#define REG_TS_ADC     0x10
#define REG_VBUS_ADC   0x11
#define REG_ICHG_ADC   0x12
#define REG_IDPM_ADC   0x13
#define REG_CTRL_3     0x14

// Máscaras para o Registrador de Status (0x0B)
#define STATUS_VBUS_STAT_MASK  0b11100000
#define STATUS_VBUS_STAT_SHIFT 5
#define STATUS_CHG_STAT_MASK   0b00011000
#define STATUS_CHG_STAT_SHIFT  3
#define STATUS_PG_STAT_MASK    0b00000100
#define STATUS_PG_STAT_SHIFT   2
#define STATUS_VSYS_STAT_MASK  0b00000001

// Máscaras do ADC (0x02)
#define ADC_CTRL_CONV_RATE_MASK 0b10000000
#define ADC_CTRL_ADC_EN_MASK    0b01000000

// Máscara para tensão da bateria
#define BATV_MASK 0b01111111

static const char *TAG = "BQ25896";

static bool s_present = false; // true once the charger has answered on I2C

static esp_err_t bq25896_read_reg(uint8_t reg, uint8_t *data) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (BQ25896_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (BQ25896_I2C_ADDR << 1) | I2C_MASTER_READ, true);
  i2c_master_read_byte(cmd, data, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

static esp_err_t bq25896_write_reg(uint8_t reg, uint8_t data) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (BQ25896_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_write_byte(cmd, data, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

esp_err_t bq25896_init(void) {
  uint8_t data;
  esp_err_t ret = bq25896_read_reg(REG_CTRL_3, &data);
  if (ret != ESP_OK) {
    s_present = false;
    ESP_LOGE(TAG, "Falha ao comunicar com o BQ25896.");
    return ret;
  }
  s_present = true;

  // Drive CE (active-low, GPIO33) LOW to ENABLE battery charging. Left undriven,
  // the charger reports "not charging" even with VBUS present.
  gpio_config_t ce_cfg = {
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = 1ULL << GPIO_CHARGER_CE_PIN,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&ce_cfg);
  gpio_set_level(GPIO_CHARGER_CE_PIN, 0);

  // REG07: (a) disable the charge watchdog ([5:4]=00) so it doesn't periodically
  // reset our ADC/charge settings back to POR defaults (which would stop the ADC);
  // (b) JEITA_ISET=0 (bit 0) so the cool-region (T1..T2) fast-charge current is
  // 50% of ICHG instead of the default 20% — faster charge while the TS reads
  // "cool" (NTC_FAULT=0b011). Note: this affects FAST charge, not the pre-charge
  // phase, which stays at the gentle IPRECHG rate for deeply-discharged cells.
  if (bq25896_read_reg(REG_CHG_CTRL_1, &data) == ESP_OK) {
    data &= ~0x30; // WATCHDOG = 00 (disabled)
    data &= ~0x01; // JEITA_ISET = 0 (cool-region current 50% of ICHG)
    bq25896_write_reg(REG_CHG_CTRL_1, data);
  }

  // REG03 SYS_MIN[3:1]: drop the minimum-system-voltage floor from the POR
  // default 3.5V (101) to 3.0V (000). In battery-only mode (no VBUS) the BQ's
  // battery-monitor ADC is only active while VBAT > SYS_MIN (datasheet p.24), so
  // at the 3.5V default the fuel gauge FREEZES once the pack drops below 3.5V
  // (never reaching low-batt/critical-shutdown). 3.0V keeps the ADC converting
  // down to the 3.0V knee. Only touches the SYS floor + ADC gate — not charge
  // current/voltage/speed. Safe here: the 3.3V P4 rail is a TPS63020 buck-boost
  // off VSYS. (For a bit more VSYS headroom, use `data |= 0x04` => 010 = 3.2V,
  // which lines up with BATTERY_MIN_MV=3200.)
  if (bq25896_read_reg(REG_CHG_CTRL_0, &data) == ESP_OK) {
    data &= ~0x0E; // SYS_MIN = 000 (3.0V)
    bq25896_write_reg(REG_CHG_CTRL_0, data);
  }

  // Continuous ADC (CONV_RATE=1) so battery voltage / status stay fresh.
  ret = bq25896_read_reg(REG_ADC_CTRL, &data);
  if (ret != ESP_OK)
    return ret;

  data |= ADC_CTRL_ADC_EN_MASK;
  data &= ~ADC_CTRL_CONV_RATE_MASK;

  ret = bq25896_write_reg(REG_ADC_CTRL, data);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "BQ25896 inicializado com sucesso.");
  }

  return ret;
}

bool bq25896_is_present(void) {
  return s_present;
}

bq25896_charge_status_t bq25896_get_charge_status(void) {
  uint8_t data = 0;
  if (bq25896_read_reg(REG_STATUS, &data) == ESP_OK) {
    uint8_t status = (data & STATUS_CHG_STAT_MASK) >> STATUS_CHG_STAT_SHIFT;
    return (bq25896_charge_status_t)status;
  }
  return CHARGE_STATUS_NOT_CHARGING;
}

bq25896_vbus_status_t bq25896_get_vbus_status(void) {
  uint8_t data = 0;
  if (bq25896_read_reg(REG_STATUS, &data) == ESP_OK) {
    uint8_t status = (data & STATUS_VBUS_STAT_MASK) >> STATUS_VBUS_STAT_SHIFT;
    return (bq25896_vbus_status_t)status;
  }
  return VBUS_STATUS_UNKNOWN;
}

bool bq25896_is_charging(void) {
  bq25896_charge_status_t status = bq25896_get_charge_status();
  return (status == CHARGE_STATUS_PRECHARGE || status == CHARGE_STATUS_FAST_CHARGE);
}

int bq25896_get_battery_percentage(uint16_t voltage_mv) {
  if (voltage_mv <= BATTERY_MIN_MV)
    return 0;
  if (voltage_mv >= BATTERY_MAX_MV)
    return 100;

  int percentage = ((voltage_mv - BATTERY_MIN_MV) * 100) / (BATTERY_MAX_MV - BATTERY_MIN_MV);
  return percentage > 100 ? 100 : percentage;
}

uint16_t bq25896_get_battery_voltage(void) {
  uint8_t data = 0;
  if (bq25896_read_reg(REG_BAT_VOLT, &data) == ESP_OK) {
    uint16_t voltage = BATV_BASE_MV + ((data & BATV_MASK) * BATV_STEP_MV);
    return voltage;
  }
  return 0;
}

uint8_t bq25896_get_fault(void) {
  uint8_t data = 0;
  bq25896_read_reg(REG_FAULT, &data);
  return data;
}
