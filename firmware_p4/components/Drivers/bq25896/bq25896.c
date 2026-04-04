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


#include "bq25896.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>

#define I2C_PORT I2C_NUM_0

// Endereço I2C do BQ25896
#define BQ25896_I2C_ADDR 0x6B

// Definições dos Registradores
#define REG_ILIM        0x00
#define REG_VINDPM      0x01
#define REG_ADC_CTRL    0x02
#define REG_CHG_CTRL_0  0x03
#define REG_ICHG        0x04
#define REG_IPRE_ITERM  0x05
#define REG_VREG        0x06
#define REG_CHG_CTRL_1  0x07
#define REG_CHG_TIMER   0x08
#define REG_BAT_COMP    0x09
#define REG_CHG_CTRL_2  0x0A
#define REG_STATUS      0x0B
#define REG_FAULT       0x0C
#define REG_VINDPM_OS   0x0D
#define REG_BAT_VOLT    0x0E
#define REG_SYS_VOLT    0x0F
#define REG_TS_ADC      0x10
#define REG_VBUS_ADC    0x11
#define REG_ICHG_ADC    0x12
#define REG_IDPM_ADC    0x13
#define REG_CTRL_3      0x14

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

// Leitura de registrador
static esp_err_t bq25896_read_reg(uint8_t reg, uint8_t *data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ25896_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ25896_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Escrita de registrador
static esp_err_t bq25896_write_reg(uint8_t reg, uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ25896_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

// Inicializa o BQ25896
esp_err_t bq25896_init(void) {
    uint8_t data;
    esp_err_t ret = bq25896_read_reg(REG_CTRL_3, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao comunicar com o BQ25896.");
        return ret;
    }

    ret = bq25896_read_reg(REG_ADC_CTRL, &data);
    if (ret != ESP_OK) return ret;

    data |= ADC_CTRL_ADC_EN_MASK;
    data &= ~ADC_CTRL_CONV_RATE_MASK;

    ret = bq25896_write_reg(REG_ADC_CTRL, data);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BQ25896 inicializado com sucesso.");
    }

    return ret;
}

// Retorna status de carregamento
bq25896_charge_status_t bq25896_get_charge_status(void) {
    uint8_t data = 0;
    if (bq25896_read_reg(REG_STATUS, &data) == ESP_OK) {
        uint8_t status = (data & STATUS_CHG_STAT_MASK) >> STATUS_CHG_STAT_SHIFT;
        return (bq25896_charge_status_t)status;
    }
    return CHARGE_STATUS_NOT_CHARGING;
}

// Retorna status do VBUS
bq25896_vbus_status_t bq25896_get_vbus_status(void) {
    uint8_t data = 0;
    if (bq25896_read_reg(REG_STATUS, &data) == ESP_OK) {
        uint8_t status = (data & STATUS_VBUS_STAT_MASK) >> STATUS_VBUS_STAT_SHIFT;
        return (bq25896_vbus_status_t)status;
    }
    return VBUS_STATUS_UNKNOWN;
}

// Verifica se está carregando
bool bq25896_is_charging(void) {
    bq25896_charge_status_t status = bq25896_get_charge_status();
    return (status == CHARGE_STATUS_PRECHARGE || status == CHARGE_STATUS_FAST_CHARGE);
}

// Calcula porcentagem estimada da bateria
int bq25896_get_battery_percentage(uint16_t voltage_mv) {
    const int min_voltage = 3200; // 0%
    const int max_voltage = 4200; // 100%

    if (voltage_mv <= min_voltage) return 0;
    if (voltage_mv >= max_voltage) return 100;

    int percentage = ((voltage_mv - min_voltage) * 100) / (max_voltage - min_voltage);
    return percentage > 100 ? 100 : percentage;
}

// Retorna tensão da bateria
uint16_t bq25896_get_battery_voltage(void) {
    uint8_t data = 0;
    if (bq25896_read_reg(REG_BAT_VOLT, &data) == ESP_OK) {
        uint16_t voltage = 2304 + ((data & BATV_MASK) * 20);
        return voltage;
    }
    return 0;
}
