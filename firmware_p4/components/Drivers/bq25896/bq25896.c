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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "i2c_init.h"
#include "pin_def.h"

#define I2C_TIMEOUT_MS 100
// Consecutive I2C failures before a bus recovery is attempted.
#define I2C_FAIL_RECOVER 3
#define BATV_BASE_MV     2304
#define BATV_STEP_MV     20
#define BATTERY_MIN_MV   3200
#define BATTERY_MAX_MV   4200

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

// REG03 CHG_CONFIG (bit 4): 1 = charging enabled. Charging is ALSO gated by the
// active-low CE pin (GPIO33) driven at init.
#define CHG_CONFIG_MASK 0b00010000

// REG09 (0x09) BATFET_DIS (bit 5): 1 = force BATFET off = ship mode / real power
// off. Note: with VBUS (USB) present the BATFET stays on, so this only powers the
// device down when running on battery. (The REG_BAT_COMP macro above mislabels
// 0x09; this is the datasheet-correct use of that register.)
//
// BATFET_RST_EN (bit 2): the "full system reset via /QON" feature. Default 1. When
// set, a /QON pulse makes the part cycle BATFET off then back ON automatically (a
// reset), so on this board (/QON = BACK button) ship mode would exit and cold-boot
// a few seconds later. Cleared on power-off so BATFET_DIS stays a permanent off.
// BATFET_DLY (bit 3): 1 = delay the BATFET turn-off so the MCU finishes the I2C
// transaction cleanly before the rail collapses.
#define REG_BATFET_CTRL    0x09
#define BATFET_DIS_MASK    0b00100000
#define BATFET_DLY_MASK    0b00001000
#define BATFET_RST_EN_MASK 0b00000100

static const char *TAG = "BQ25896";

static bool s_present = false; // true once the charger has answered on I2C

static i2c_master_dev_handle_t s_dev = NULL;
static uint32_t s_i2c_fail_streak = 0;

// Reactive bus recovery: a run of failed transfers usually means a slave wedged
// the bus (held SDA low). Reset it and start over so the charger does not stay
// unreadable for the rest of the session.
static void note_i2c_result(esp_err_t ret) {
  if (ret == ESP_OK) {
    s_i2c_fail_streak = 0;
    return;
  }
  if (++s_i2c_fail_streak >= I2C_FAIL_RECOVER) {
    s_i2c_fail_streak = 0;
    i2c_bus_recover();
  }
}

static esp_err_t bq25896_read_reg(uint8_t reg, uint8_t *data) {
  if (s_dev == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, data, 1, I2C_TIMEOUT_MS);
  note_i2c_result(ret);
  return ret;
}

static esp_err_t bq25896_write_reg(uint8_t reg, uint8_t data) {
  if (s_dev == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  uint8_t buf[2] = {reg, data};
  esp_err_t ret = i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
  note_i2c_result(ret);
  return ret;
}

esp_err_t bq25896_init(void) {
  if (s_dev == NULL) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BQ25896_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t add = i2c_master_bus_add_device(i2c_get_bus(), &dev_cfg, &s_dev);
    if (add != ESP_OK) {
      s_present = false;
      ESP_LOGE(TAG, "bq25896 add_device failed: %s", esp_err_to_name(add));
      return add;
    }
  }

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

uint8_t bq25896_reg_raw(uint8_t reg) {
  uint8_t data = 0;
  bq25896_read_reg(reg, &data);
  return data;
}

bool bq25896_get_charge_enable(void) {
  uint8_t data = 0;
  if (bq25896_read_reg(REG_CHG_CTRL_0, &data) == ESP_OK) {
    return (data & CHG_CONFIG_MASK) != 0;
  }
  return false;
}

esp_err_t bq25896_set_charge_enable(bool enable) {
  // Two gates: the active-low CE pin (GPIO33) and REG03 CHG_CONFIG. Drive both so
  // the state is unambiguous.
  gpio_set_level(GPIO_CHARGER_CE_PIN, enable ? 0 : 1);

  uint8_t data = 0;
  esp_err_t ret = bq25896_read_reg(REG_CHG_CTRL_0, &data);
  if (ret != ESP_OK) {
    return ret;
  }
  if (enable) {
    data |= CHG_CONFIG_MASK;
  } else {
    data &= ~CHG_CONFIG_MASK;
  }
  return bq25896_write_reg(REG_CHG_CTRL_0, data);
}

esp_err_t bq25896_power_off(void) {
  // Real ship mode: set BATFET_DIS (REG09 bit5) to disconnect the battery. With
  // VBUS present the BATFET stays on (the part keeps the system powered from USB),
  // so this only powers the device off when running on battery.
  uint8_t data = 0;
  esp_err_t ret = bq25896_read_reg(REG_BATFET_CTRL, &data);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "power_off: could not read BATFET reg: %s", esp_err_to_name(ret));
    return ret;
  }
  data |= BATFET_DIS_MASK;     // force BATFET off (ship mode)
  data |= BATFET_DLY_MASK;     // delayed turn-off: finish this I2C write first
  data &= ~BATFET_RST_EN_MASK; // disarm the /QON auto-reset so it stays off
  ret = bq25896_write_reg(REG_BATFET_CTRL, data);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "power_off: BATFET_DIS write failed: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGW(TAG, "Ship mode: BATFET disabled (powering off if on battery)");
  }
  return ret;
}
