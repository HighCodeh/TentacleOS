// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "bq25896.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "i2c_init.h"
#include "pin_def.h"

#define I2C_TIMEOUT_MS      100
#define DRIVER_LOCK_MS      300
#define I2C_FAIL_RECOVER    3
#define BATTERY_MIN_MV      3200
#define BATTERY_MAX_MV      4200
#define BQ25896_EXPECTED_PN 0

enum {
  REG00_INPUT = 0x00,
  REG01_VINDPM_OFFSET = 0x01,
  REG02_ADC_CONTROL = 0x02,
  REG03_POWER_CONTROL = 0x03,
  REG04_CHARGE_CURRENT = 0x04,
  REG05_PRE_TERM_CURRENT = 0x05,
  REG06_CHARGE_VOLTAGE = 0x06,
  REG07_TIMER_CONTROL = 0x07,
  REG08_THERMAL_IR = 0x08,
  REG09_MISC_CONTROL = 0x09,
  REG0A_BOOST_CONTROL = 0x0A,
  REG0B_STATUS = 0x0B,
  REG0C_FAULT = 0x0C,
  REG0D_VINDPM = 0x0D,
  REG0E_BATTERY_ADC = 0x0E,
  REG0F_SYSTEM_ADC = 0x0F,
  REG10_TS_ADC = 0x10,
  REG11_VBUS_ADC = 0x11,
  REG12_CHARGE_ADC = 0x12,
  REG13_DPM_STATUS = 0x13,
  REG14_PART_INFO = 0x14,
};

#define REG00_EN_HIZ           0x80
#define REG00_EN_ILIM          0x40
#define REG00_IINLIM_MASK      0x3F
#define REG02_CONV_START       0x80
#define REG02_CONV_RATE        0x40
#define REG02_ICO_EN           0x10
#define REG03_OTG_CONFIG       0x20
#define REG03_CHG_CONFIG       0x10
#define REG03_SYS_MIN_MASK     0x0E
#define REG03_SYS_MIN_3200     0x04
#define REG04_ICHG_MASK        0x7F
#define REG06_VREG_MASK        0xFC
#define REG07_WATCHDOG_MASK    0x30
#define REG07_JEITA_ISET       0x01
#define REG09_FORCE_ICO        0x80
#define REG09_BATFET_DIS       0x20
#define REG09_BATFET_DLY       0x08
#define REG09_BATFET_RST_EN    0x04
#define REG0A_BOOSTV_MASK      0xF0
#define REG0A_BOOST_LIM_MASK   0x07
#define REG0B_VBUS_STAT_MASK   0xE0
#define REG0B_CHARGE_STAT_MASK 0x18
#define REG0B_POWER_GOOD       0x04
#define REG0B_VSYS_STAT        0x01
#define REG0E_THERM_STAT       0x80
#define REG11_VBUS_GOOD        0x80
#define REG13_VDPM_STAT        0x80
#define REG13_IDPM_STAT        0x40
#define REG13_IDPM_LIM_MASK    0x3F
#define REG14_REG_RESET        0x80
#define REG14_ICO_OPTIMIZED    0x40
#define REG14_PART_NUMBER_MASK 0x38
#define REG14_REVISION_MASK    0x03

#define FAULT_WATCHDOG    0x80
#define FAULT_BOOST       0x40
#define FAULT_CHARGE_MASK 0x30
#define FAULT_BATTERY_OVP 0x08
#define FAULT_NTC_MASK    0x07

/* Flipper's stable OTG target: 5.062 V, 1.4 A. */
#define OTG_DEFAULT_VOLTAGE_CODE 8
#define OTG_DEFAULT_CURRENT_CODE 3
/* Use 2.15 A for 30 ms, then restore the requested limit. */
#define OTG_START_CURRENT_CODE 6

static const char *TAG = "BQ25896";
static const uint16_t s_boost_current_ma[8] = {500, 750, 1200, 1400, 1650, 1875, 2150, 2150};

static i2c_master_dev_handle_t s_dev = NULL;
static SemaphoreHandle_t s_lock = NULL;
static bool s_present = false;
static uint32_t s_i2c_fail_streak = 0;
static uint32_t s_i2c_error_count = 0;
static uint8_t s_otg_voltage_code = OTG_DEFAULT_VOLTAGE_CODE;
static uint8_t s_otg_current_code = OTG_DEFAULT_CURRENT_CODE;

static esp_err_t ensure_lock(void) {
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutex();
  }
  return s_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t lock_driver(void) {
  esp_err_t ret = ensure_lock();
  if (ret != ESP_OK) {
    return ret;
  }
  return xSemaphoreTake(s_lock, pdMS_TO_TICKS(DRIVER_LOCK_MS)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void unlock_driver(void) {
  xSemaphoreGive(s_lock);
}

static void note_i2c_result(esp_err_t ret) {
  if (ret == ESP_OK) {
    s_i2c_fail_streak = 0;
    return;
  }

  s_i2c_error_count++;
  if (++s_i2c_fail_streak >= I2C_FAIL_RECOVER) {
    s_i2c_fail_streak = 0;
    (void)i2c_bus_recover();
  }
}

static esp_err_t read_reg_unlocked(uint8_t reg, uint8_t *value) {
  if (s_dev == NULL || value == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, I2C_TIMEOUT_MS);
  note_i2c_result(ret);
  return ret;
}

static esp_err_t write_reg_unlocked(uint8_t reg, uint8_t value) {
  if (s_dev == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  uint8_t packet[2] = {reg, value};
  esp_err_t ret = i2c_master_transmit(s_dev, packet, sizeof(packet), I2C_TIMEOUT_MS);
  note_i2c_result(ret);
  return ret;
}

static esp_err_t update_reg_unlocked(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current = 0;
  esp_err_t ret = read_reg_unlocked(reg, &current);
  if (ret != ESP_OK) {
    return ret;
  }
  uint8_t updated = (current & (uint8_t)~mask) | (value & mask);
  return updated == current ? ESP_OK : write_reg_unlocked(reg, updated);
}

static esp_err_t configure_otg_pin_low(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << GPIO_CHARGER_OTG_PIN,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t ret = gpio_config(&cfg);
  if (ret == ESP_OK) {
    ret = gpio_set_level(GPIO_CHARGER_OTG_PIN, 0);
  }
  return ret;
}

static esp_err_t apply_runtime_config_unlocked(void) {
  esp_err_t ret;

  /* Start a conversion and select continuous mode. CONV_START is a command
   * bit: hardware clears it after the first conversion, while CONV_RATE stays
   * set for subsequent one-second conversions. */
  ret = update_reg_unlocked(
      REG02_ADC_CONTROL, REG02_CONV_START | REG02_CONV_RATE, REG02_CONV_START | REG02_CONV_RATE);
  if (ret != ESP_OK) {
    return ret;
  }

  /* OTG off at boot; 3.2 V keeps ADC useful down to the product's empty
   * threshold. */
  ret = update_reg_unlocked(
      REG03_POWER_CONTROL, REG03_OTG_CONFIG | REG03_SYS_MIN_MASK, REG03_SYS_MIN_3200);
  if (ret != ESP_OK) {
    return ret;
  }

  /* Disable only the I2C watchdog; retain the safety timer and conservative
   * JEITA 20%. */
  ret = update_reg_unlocked(
      REG07_TIMER_CONTROL, REG07_WATCHDOG_MASK | REG07_JEITA_ISET, REG07_JEITA_ISET);
  if (ret != ESP_OK) {
    return ret;
  }

  /* Known OTG profile, matching Flipper Zero: 5.062 V / 1.4 A. */
  s_otg_voltage_code = OTG_DEFAULT_VOLTAGE_CODE;
  s_otg_current_code = OTG_DEFAULT_CURRENT_CODE;
  ret = update_reg_unlocked(REG0A_BOOST_CONTROL,
                            REG0A_BOOSTV_MASK | REG0A_BOOST_LIM_MASK,
                            (uint8_t)(s_otg_voltage_code << 4) | s_otg_current_code);
  return ret;
}

static esp_err_t read_all_unlocked(uint8_t regs[BQ25896_REGISTER_COUNT], uint8_t *fault_latched) {
  for (uint8_t reg = 0; reg < BQ25896_REGISTER_COUNT; reg++) {
    esp_err_t ret;
    if (reg == REG0C_FAULT) {
      /* TI requires consecutive reads: first is latched history, second is
       * live. */
      ret = read_reg_unlocked(reg, fault_latched);
      if (ret == ESP_OK) {
        ret = read_reg_unlocked(reg, &regs[reg]);
      }
    } else {
      ret = read_reg_unlocked(reg, &regs[reg]);
    }
    if (ret != ESP_OK) {
      return ret;
    }
  }
  return ESP_OK;
}

static uint16_t decode_boost_current(uint8_t code) {
  return s_boost_current_ma[code & REG0A_BOOST_LIM_MASK];
}

esp_err_t bq25896_init(void) {
  esp_err_t ret = lock_driver();
  if (ret != ESP_OK) {
    return ret;
  }
  if (s_present) {
    unlock_driver();
    return ESP_OK;
  }

  ret = configure_otg_pin_low();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "OTG GPIO init failed: %s", esp_err_to_name(ret));
    unlock_driver();
    return ret;
  }

  if (s_dev == NULL) {
    i2c_master_bus_handle_t bus = i2c_get_bus();
    if (bus == NULL) {
      unlock_driver();
      return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BQ25896_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
      s_present = false;
      unlock_driver();
      ESP_LOGE(TAG, "add_device failed: %s", esp_err_to_name(ret));
      return ret;
    }
  }

  uint8_t part = 0;
  ret = read_reg_unlocked(REG14_PART_INFO, &part);
  if (ret != ESP_OK) {
    s_present = false;
    unlock_driver();
    ESP_LOGE(TAG, "U4 did not answer at 0x%02X: %s", BQ25896_I2C_ADDR, esp_err_to_name(ret));
    return ret;
  }

  uint8_t pn = (part & REG14_PART_NUMBER_MASK) >> 3;
  if (pn != BQ25896_EXPECTED_PN) {
    s_present = false;
    unlock_driver();
    ESP_LOGE(TAG, "unexpected BQ2589x PN=%u (REG14=0x%02X)", pn, part);
    return ESP_ERR_NOT_FOUND;
  }

  /* Like Flipper, reset retained charger state after an MCU-only reboot. */
  ret = write_reg_unlocked(REG14_PART_INFO, REG14_REG_RESET);
  if (ret == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(2));
    ret = apply_runtime_config_unlocked();
  }

  if (ret == ESP_OK) {
    uint8_t verify_adc = 0;
    uint8_t verify_timer = 0;
    ret = read_reg_unlocked(REG02_ADC_CONTROL, &verify_adc);
    if (ret == ESP_OK) {
      ret = read_reg_unlocked(REG07_TIMER_CONTROL, &verify_timer);
    }
    if (ret == ESP_OK &&
        ((verify_adc & REG02_CONV_RATE) == 0 || (verify_timer & REG07_WATCHDOG_MASK) != 0)) {
      ret = ESP_ERR_INVALID_RESPONSE;
    }
  }

  s_present = ret == ESP_OK;
  unlock_driver();

  if (ret == ESP_OK) {
    ESP_LOGI(TAG,
             "BQ25896 PN=%u rev=%u ready: ADC continuous, SYS_MIN=3.2V, "
             "watchdog off, OTG 5.062V/1.4A",
             pn,
             part & REG14_REVISION_MASK);
  } else {
    ESP_LOGE(TAG, "initialization failed: %s", esp_err_to_name(ret));
  }
  return ret;
}

bool bq25896_is_present(void) {
  return s_present;
}

esp_err_t bq25896_reapply_runtime_config(void) {
  esp_err_t ret = configure_otg_pin_low();
  if (ret != ESP_OK) {
    return ret;
  }
  ret = lock_driver();
  if (ret == ESP_OK) {
    ret = apply_runtime_config_unlocked();
    unlock_driver();
  }
  return ret;
}

esp_err_t bq25896_read_register(uint8_t reg, uint8_t *value) {
  if (reg >= BQ25896_REGISTER_COUNT || value == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = read_reg_unlocked(reg, value);
    unlock_driver();
  }
  return ret;
}

esp_err_t bq25896_dump_registers(uint8_t regs[BQ25896_REGISTER_COUNT]) {
  if (regs == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t latched = 0;
  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = read_all_unlocked(regs, &latched);
    unlock_driver();
  }
  return ret;
}

esp_err_t bq25896_read_telemetry(bq25896_telem_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t regs[BQ25896_REGISTER_COUNT] = {0};
  uint8_t fault_latched = 0;
  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = read_all_unlocked(regs, &fault_latched);
    unlock_driver();
  }
  if (ret != ESP_OK) {
    return ret;
  }

  memset(out, 0, sizeof(*out));
  const uint8_t status = regs[REG0B_STATUS];
  const uint8_t fault_live = regs[REG0C_FAULT];
  const uint8_t fault_for_state = fault_live != 0 ? fault_live : fault_latched;
  const uint8_t vbus_code = (status & REG0B_VBUS_STAT_MASK) >> 5;
  const uint8_t charge_code = (status & REG0B_CHARGE_STAT_MASK) >> 3;

  out->vbat_mv = 2304 + (uint16_t)(regs[REG0E_BATTERY_ADC] & 0x7F) * 20;
  out->vsys_mv = 2304 + (uint16_t)(regs[REG0F_SYSTEM_ADC] & 0x7F) * 20;
  out->vbus_good = (regs[REG11_VBUS_ADC] & REG11_VBUS_GOOD) != 0;
  out->vbus_mv = out->vbus_good ? 2600 + (uint16_t)(regs[REG11_VBUS_ADC] & 0x7F) * 100 : 0;
  out->ichg_ma = (uint16_t)(regs[REG12_CHARGE_ADC] & 0x7F) * 50;
  out->ts_milli_pct = 21000 + (uint32_t)(regs[REG10_TS_ADC] & 0x7F) * 465;
  out->temperature_decic = (int16_t)((71000 - (int32_t)out->ts_milli_pct) / 60);

  out->input_current_limit_ma = 100 + (uint16_t)(regs[REG00_INPUT] & REG00_IINLIM_MASK) * 50;
  out->effective_input_limit_ma =
      (regs[REG02_ADC_CONTROL] & REG02_ICO_EN)
          ? 100 + (uint16_t)(regs[REG13_DPM_STATUS] & REG13_IDPM_LIM_MASK) * 50
          : out->input_current_limit_ma;
  uint16_t charge_limit = (uint16_t)(regs[REG04_CHARGE_CURRENT] & REG04_ICHG_MASK) * 64;
  out->charge_current_limit_ma = charge_limit > 3008 ? 3008 : charge_limit;
  out->precharge_current_limit_ma =
      64 + (uint16_t)((regs[REG05_PRE_TERM_CURRENT] >> 4) & 0x0F) * 64;
  out->termination_current_limit_ma = 64 + (uint16_t)(regs[REG05_PRE_TERM_CURRENT] & 0x0F) * 64;
  out->charge_voltage_limit_mv =
      3840 + (uint16_t)((regs[REG06_CHARGE_VOLTAGE] & REG06_VREG_MASK) >> 2) * 16;
  out->input_voltage_limit_mv = 2600 + (uint16_t)(regs[REG0D_VINDPM] & 0x7F) * 100;
  out->boost_voltage_mv =
      4550 + (uint16_t)((regs[REG0A_BOOST_CONTROL] & REG0A_BOOSTV_MASK) >> 4) * 64;
  out->boost_current_limit_ma = decode_boost_current(regs[REG0A_BOOST_CONTROL]);
  out->system_min_voltage_mv =
      3000 + (uint16_t)((regs[REG03_POWER_CONTROL] & REG03_SYS_MIN_MASK) >> 1) * 100;

  out->raw_status = status;
  out->fault_latched = fault_latched;
  out->fault_live = fault_live;
  out->fault = fault_latched | fault_live;
  out->raw_part_info = regs[REG14_PART_INFO];
  out->part_number = (regs[REG14_PART_INFO] & REG14_PART_NUMBER_MASK) >> 3;
  out->device_revision = regs[REG14_PART_INFO] & REG14_REVISION_MASK;
  out->soc = (uint8_t)bq25896_get_battery_percentage(out->vbat_mv);
  out->vbus = (bq25896_vbus_status_t)vbus_code;
  out->chg = (bq25896_charge_status_t)charge_code;
  out->charge_fault = (bq25896_charge_fault_t)((fault_for_state & FAULT_CHARGE_MASK) >> 4);
  out->ntc_state = (bq25896_ntc_state_t)(fault_for_state & FAULT_NTC_MASK);

  out->charging =
      charge_code == CHARGE_STATUS_PRECHARGE || charge_code == CHARGE_STATUS_FAST_CHARGE;
  out->charge_enabled = (regs[REG03_POWER_CONTROL] & REG03_CHG_CONFIG) != 0;
  out->otg_enabled = (regs[REG03_POWER_CONTROL] & REG03_OTG_CONFIG) != 0 &&
                     gpio_get_level(GPIO_CHARGER_OTG_PIN) != 0;
  out->hiz_enabled = (regs[REG00_INPUT] & REG00_EN_HIZ) != 0;
  out->power_good =
      vbus_code != VBUS_STATUS_OTG && (((status & REG0B_POWER_GOOD) != 0) || out->vbus_good);
  /* CONV_START self-clears; CONV_RATE is the persistent continuous-mode
   * indicator. */
  out->adc_continuous = (regs[REG02_ADC_CONTROL] & REG02_CONV_RATE) != 0;
  out->ico_enabled = (regs[REG02_ADC_CONTROL] & REG02_ICO_EN) != 0;
  out->ico_optimized = (regs[REG14_PART_INFO] & REG14_ICO_OPTIMIZED) != 0;
  out->thermal_regulation = (regs[REG0E_BATTERY_ADC] & REG0E_THERM_STAT) != 0;
  out->vsys_regulation = (status & REG0B_VSYS_STAT) != 0;
  out->input_current_dpm = (regs[REG13_DPM_STATUS] & REG13_IDPM_STAT) != 0;
  out->input_voltage_dpm = (regs[REG13_DPM_STATUS] & REG13_VDPM_STAT) != 0;
  out->watchdog_fault = (out->fault & FAULT_WATCHDOG) != 0;
  out->boost_fault = (out->fault & FAULT_BOOST) != 0;
  out->battery_ovp = (out->fault & FAULT_BATTERY_OVP) != 0;
  return ESP_OK;
}

bq25896_charge_status_t bq25896_get_charge_status(void) {
  uint8_t value = 0;
  if (bq25896_read_register(REG0B_STATUS, &value) != ESP_OK) {
    return CHARGE_STATUS_NOT_CHARGING;
  }
  return (bq25896_charge_status_t)((value & REG0B_CHARGE_STAT_MASK) >> 3);
}

bq25896_vbus_status_t bq25896_get_vbus_status(void) {
  uint8_t value = 0;
  if (bq25896_read_register(REG0B_STATUS, &value) != ESP_OK) {
    return VBUS_STATUS_UNKNOWN;
  }
  return (bq25896_vbus_status_t)((value & REG0B_VBUS_STAT_MASK) >> 5);
}

bool bq25896_is_charging(void) {
  bq25896_charge_status_t state = bq25896_get_charge_status();
  return state == CHARGE_STATUS_PRECHARGE || state == CHARGE_STATUS_FAST_CHARGE;
}

int bq25896_get_battery_percentage(uint16_t voltage_mv) {
  if (voltage_mv <= BATTERY_MIN_MV) {
    return 0;
  }
  if (voltage_mv >= BATTERY_MAX_MV) {
    return 100;
  }
  return ((int)voltage_mv - BATTERY_MIN_MV) * 100 / (BATTERY_MAX_MV - BATTERY_MIN_MV);
}

uint16_t bq25896_get_battery_voltage(void) {
  uint8_t value = 0;
  if (bq25896_read_register(REG0E_BATTERY_ADC, &value) != ESP_OK) {
    return 0;
  }
  return 2304 + (uint16_t)(value & 0x7F) * 20;
}

uint8_t bq25896_get_fault(void) {
  uint8_t latched = 0;
  uint8_t live = 0;
  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = read_reg_unlocked(REG0C_FAULT, &latched);
    if (ret == ESP_OK) {
      ret = read_reg_unlocked(REG0C_FAULT, &live);
    }
    unlock_driver();
  }
  return ret == ESP_OK ? (latched | live) : 0;
}

bool bq25896_get_charge_enable(void) {
  uint8_t value = 0;
  return bq25896_read_register(REG03_POWER_CONTROL, &value) == ESP_OK &&
         (value & REG03_CHG_CONFIG) != 0;
}

esp_err_t bq25896_set_charge_enable(bool enable) {
  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = update_reg_unlocked(REG03_POWER_CONTROL, REG03_CHG_CONFIG, enable ? REG03_CHG_CONFIG : 0);
    unlock_driver();
  }
  return ret;
}

esp_err_t bq25896_set_input_current_limit(uint16_t requested_ma, uint16_t *actual_ma) {
  uint16_t clamped = requested_ma < 100 ? 100 : (requested_ma > 3250 ? 3250 : requested_ma);
  uint8_t code = (uint8_t)((clamped - 100) / 50);
  uint16_t actual = 100 + (uint16_t)code * 50;
  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = update_reg_unlocked(REG00_INPUT, REG00_IINLIM_MASK, code);
    unlock_driver();
  }
  if (actual_ma != NULL) {
    *actual_ma = actual;
  }
  return ret;
}

esp_err_t bq25896_set_charge_current_limit(uint16_t requested_ma, uint16_t *actual_ma) {
  uint16_t clamped = requested_ma > 3008 ? 3008 : requested_ma;
  uint8_t code = (uint8_t)(clamped / 64);
  uint16_t actual = (uint16_t)code * 64;
  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = update_reg_unlocked(REG04_CHARGE_CURRENT, REG04_ICHG_MASK, code);
    unlock_driver();
  }
  if (actual_ma != NULL) {
    *actual_ma = actual;
  }
  return ret;
}

esp_err_t bq25896_set_charge_voltage_limit(uint16_t requested_mv, uint16_t *actual_mv) {
  /* Match Flipper's single-cell safety ceiling even though silicon allows more.
   */
  uint16_t clamped = requested_mv < 3840 ? 3840 : (requested_mv > 4208 ? 4208 : requested_mv);
  uint8_t code = (uint8_t)((clamped - 3840) / 16);
  uint16_t actual = 3840 + (uint16_t)code * 16;
  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = update_reg_unlocked(REG06_CHARGE_VOLTAGE, REG06_VREG_MASK, (uint8_t)(code << 2));
    unlock_driver();
  }
  if (actual_mv != NULL) {
    *actual_mv = actual;
  }
  return ret;
}

esp_err_t bq25896_set_otg_profile(uint16_t requested_mv,
                                  uint16_t requested_ma,
                                  uint16_t *actual_mv,
                                  uint16_t *actual_ma) {
  uint16_t voltage = requested_mv < 4550 ? 4550 : (requested_mv > 5510 ? 5510 : requested_mv);
  uint8_t voltage_code = (uint8_t)((voltage - 4550) / 64);
  uint8_t current_code = 0;
  for (uint8_t i = 1; i < 7; i++) {
    if (s_boost_current_ma[i] > requested_ma) {
      break;
    }
    current_code = i;
  }

  esp_err_t ret = lock_driver();
  if (ret == ESP_OK) {
    ret = update_reg_unlocked(REG0A_BOOST_CONTROL,
                              REG0A_BOOSTV_MASK | REG0A_BOOST_LIM_MASK,
                              (uint8_t)(voltage_code << 4) | current_code);
    if (ret == ESP_OK) {
      s_otg_voltage_code = voltage_code;
      s_otg_current_code = current_code;
    }
    unlock_driver();
  }
  if (actual_mv != NULL) {
    *actual_mv = 4550 + (uint16_t)voltage_code * 64;
  }
  if (actual_ma != NULL) {
    *actual_ma = decode_boost_current(current_code);
  }
  return ret;
}

esp_err_t bq25896_set_otg_enable(bool enable) {
  esp_err_t ret = lock_driver();
  if (ret != ESP_OK) {
    return ret;
  }

  if (!enable) {
    ret = update_reg_unlocked(REG03_POWER_CONTROL, REG03_OTG_CONFIG, 0);
    (void)gpio_set_level(GPIO_CHARGER_OTG_PIN, 0);
    unlock_driver();
    return ret;
  }

  uint8_t vbus_adc = 0;
  ret = read_reg_unlocked(REG11_VBUS_ADC, &vbus_adc);
  if (ret != ESP_OK || (vbus_adc & REG11_VBUS_GOOD) != 0) {
    unlock_driver();
    return ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
  }

  /* Flipper's capacitive-load startup: 2.15 A briefly, then the requested
   * limit. */
  ret = update_reg_unlocked(REG0A_BOOST_CONTROL, REG0A_BOOST_LIM_MASK, OTG_START_CURRENT_CODE);
  if (ret == ESP_OK) {
    ret = gpio_set_level(GPIO_CHARGER_OTG_PIN, 1);
  }
  if (ret == ESP_OK) {
    ret = update_reg_unlocked(REG03_POWER_CONTROL, REG03_OTG_CONFIG, REG03_OTG_CONFIG);
  }
  if (ret == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(35));
    uint8_t status = 0;
    uint8_t fault_latched = 0;
    uint8_t fault = 0;
    ret = read_reg_unlocked(REG0B_STATUS, &status);
    if (ret == ESP_OK) {
      ret = read_reg_unlocked(REG0C_FAULT, &fault_latched);
    }
    if (ret == ESP_OK) {
      ret = read_reg_unlocked(REG0C_FAULT, &fault);
    }
    bool active = ((status & REG0B_VBUS_STAT_MASK) >> 5) == VBUS_STATUS_OTG;
    if (ret != ESP_OK || !active || (fault & FAULT_BOOST) != 0) {
      (void)update_reg_unlocked(REG03_POWER_CONTROL, REG03_OTG_CONFIG, 0);
      (void)gpio_set_level(GPIO_CHARGER_OTG_PIN, 0);
      if (ret == ESP_OK) {
        ret = ESP_FAIL;
      }
    }
  }

  (void)update_reg_unlocked(REG0A_BOOST_CONTROL, REG0A_BOOST_LIM_MASK, s_otg_current_code);
  unlock_driver();
  return ret;
}

bool bq25896_get_otg_enable(void) {
  uint8_t value = 0;
  return bq25896_read_register(REG03_POWER_CONTROL, &value) == ESP_OK &&
         (value & REG03_OTG_CONFIG) != 0 && gpio_get_level(GPIO_CHARGER_OTG_PIN) != 0;
}

esp_err_t bq25896_power_off(void) {
  esp_err_t ret = lock_driver();
  if (ret != ESP_OK) {
    return ret;
  }
  (void)update_reg_unlocked(REG03_POWER_CONTROL, REG03_OTG_CONFIG, 0);
  (void)gpio_set_level(GPIO_CHARGER_OTG_PIN, 0);
  ret = update_reg_unlocked(REG09_MISC_CONTROL,
                            REG09_BATFET_DIS | REG09_BATFET_DLY | REG09_BATFET_RST_EN,
                            REG09_BATFET_DIS | REG09_BATFET_DLY);
  unlock_driver();
  if (ret == ESP_OK) {
    ESP_LOGW(TAG, "ship mode requested; power falls when external VBUS is removed");
  }
  return ret;
}

uint8_t bq25896_reg_raw(uint8_t reg) {
  uint8_t value = 0;
  (void)bq25896_read_register(reg, &value);
  return value;
}
