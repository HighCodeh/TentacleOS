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

#include "husb320.h"

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "i2c_init.h"

#define HUSB320_ADDRESS         0x42
#define HUSB320_DEVICE_TYPE_REG 0x02
#define HUSB320_DEVICE_TYPE     0x03
#define HUSB320_PORTROLE_REG    0x03
#define HUSB320_CONTROL1_REG    0x05
#define HUSB320_ROLE_MASK       0x3F
#define HUSB320_ROLE_SINK       0x02
#define HUSB320_ENABLE          0x08
#define HUSB320_TIMEOUT_MS      100

static const char *TAG = "HUSB320";

static esp_err_t read_reg(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *out);
static esp_err_t write_reg(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value);

esp_err_t husb320_init(void) {
  i2c_master_bus_handle_t bus = i2c_get_bus();
  if (bus == NULL)
    return ESP_ERR_INVALID_STATE;
  const i2c_device_config_t config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = HUSB320_ADDRESS,
      .scl_speed_hz = I2C_MASTER_FREQ_HZ,
  };
  i2c_master_dev_handle_t device = NULL;
  esp_err_t ret = i2c_master_bus_add_device(bus, &config, &device);
  if (ret != ESP_OK)
    return ret;
  uint8_t type = 0, role = 0, control = 0, check = 0;
  ret = read_reg(device, HUSB320_DEVICE_TYPE_REG, &type);
  if (ret != ESP_OK)
    goto cleanup;
  if (type != HUSB320_DEVICE_TYPE) {
    ret = ESP_ERR_NOT_SUPPORTED;
    goto cleanup;
  }
  ret = read_reg(device, HUSB320_PORTROLE_REG, &role);
  if (ret != ESP_OK)
    goto cleanup;
  ret = read_reg(device, HUSB320_CONTROL1_REG, &control);
  if (ret != ESP_OK)
    goto cleanup;
  if ((role & HUSB320_ROLE_MASK) != HUSB320_ROLE_SINK) {
    ret = write_reg(device, HUSB320_CONTROL1_REG, control & ~HUSB320_ENABLE);
    if (ret != ESP_OK)
      goto cleanup;
    ret = write_reg(device, HUSB320_PORTROLE_REG, (role & ~HUSB320_ROLE_MASK) | HUSB320_ROLE_SINK);
    if (ret != ESP_OK)
      goto cleanup;
  }
  ret = read_reg(device, HUSB320_PORTROLE_REG, &check);
  if (ret != ESP_OK)
    goto cleanup;
  if ((check & HUSB320_ROLE_MASK) != HUSB320_ROLE_SINK) {
    ret = ESP_ERR_INVALID_RESPONSE;
    goto cleanup;
  }
  ret = write_reg(device, HUSB320_CONTROL1_REG, control | HUSB320_ENABLE);
  if (ret != ESP_OK)
    goto cleanup;
  ret = read_reg(device, HUSB320_CONTROL1_REG, &check);
  if (ret == ESP_OK && !(check & HUSB320_ENABLE))
    ret = ESP_ERR_INVALID_RESPONSE;
cleanup: {
  esp_err_t release = i2c_master_bus_rm_device(device);
  if (ret == ESP_OK)
    ret = release;
}
  if (ret == ESP_OK)
    ESP_LOGI(TAG, "Type-C sink policy confirmed at 0x42");
  else
    ESP_LOGW(TAG, "Type-C policy unconfirmed: %s", esp_err_to_name(ret));
  return ret;
}

static esp_err_t read_reg(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *out) {
  return i2c_master_transmit_receive(device, &reg, 1, out, 1, HUSB320_TIMEOUT_MS);
}

static esp_err_t write_reg(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  return i2c_master_transmit(device, data, sizeof(data), HUSB320_TIMEOUT_MS);
}
