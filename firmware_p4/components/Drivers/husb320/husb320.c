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
#include <stdatomic.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "i2c_init.h"

// Datasheet Table 11 includes R/W in 0x42; ESP-IDF takes the seven-bit address.
#define HUSB320_ADDRESS              0x21
#define HUSB320_DEVICE_TYPE_REG      0x02
#define HUSB320_DEVICE_TYPE          0x03
#define HUSB320_PORTROLE_REG         0x03
#define HUSB320_CONTROL1_REG         0x05
#define HUSB320_ROLE_MASK            0x3F
#define HUSB320_ROLE_SINK            0x02
#define HUSB320_ROLE_SOURCE          0x01
#define HUSB320_CONTROL_REG          0x04
#define HUSB320_HOST_CURRENT_MASK    0x06
#define HUSB320_HOST_CURRENT_DEFAULT 0x02
#define HUSB320_TYPE_SOURCE          0x08
#define HUSB320_TYPE_ACTIVE_CABLE    0x04
#define HUSB320_ENABLE               0x08
#define HUSB320_TIMEOUT_MS           15
#define HUSB320_HOST_READ_ATTEMPTS   2
#define HUSB320_HOST_RETRY_MS        1
#define HUSB320_STATUS_REG           0x11
#define HUSB320_TYPE_REG             0x13
#define HUSB320_TYPE_SINK            0x10
#define HUSB320_STATUS_ATTACH        0x01
#define HUSB320_STATUS_VBUS          0x08
#define HUSB320_STATUS_SAFE0V        0x40
#define HUSB320_ORIENT_MASK          0x30
#define HUSB320_ORIENT_CC1           0x10
#define HUSB320_ORIENT_CC2           0x20
#define HUSB320_LEVEL_MASK           0x06
#define HUSB320_LEVEL_DEFAULT        0x02
#define HUSB320_LEVEL_1500           0x04
#define HUSB320_LEVEL_3000           0x06
#define HUSB320_CURRENT_1500_MA      1500
#define HUSB320_CURRENT_3000_MA      3000

static const char *TAG = "HUSB320";
static i2c_master_dev_handle_t s_device;
static SemaphoreHandle_t s_lock;
static bool s_is_ready;
static atomic_bool s_host_mode;
static esp_err_t s_init_error = ESP_ERR_INVALID_STATE;
static uint8_t s_last_reg;

static esp_err_t read_reg(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *out);
static esp_err_t write_reg(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value);

esp_err_t husb320_init(void) {
  // Init is serialized by boot, then the USB power worker for retries.
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
      ESP_LOGE(TAG, "Lock allocation failed");
      return ESP_ERR_NO_MEM;
    }
  }
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(HUSB320_TIMEOUT_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  if (atomic_load(&s_host_mode)) {
    xSemaphoreGive(s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = ESP_OK;
  s_is_ready = false;
  if (s_device == NULL) {
    i2c_master_bus_handle_t bus = i2c_get_bus();
    if (bus == NULL) {
      ret = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HUSB320_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(bus, &config, &s_device);
    if (ret != ESP_OK)
      goto cleanup;
  }
  i2c_master_dev_handle_t device = s_device;
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
cleanup:
  s_init_error = ret;
  s_is_ready = ret == ESP_OK;
  xSemaphoreGive(s_lock);
  return ret;
}

esp_err_t husb320_read_source(husb320_source_t *out_source) {
  if (out_source == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_source = (husb320_source_t){0};
  if (s_lock == NULL)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(HUSB320_TIMEOUT_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  esp_err_t ret = s_init_error;
  if (!s_is_ready)
    goto cleanup;
  uint8_t status = 0, check = 0, type = 0, role = 0, control = 0;
  ret = read_reg(s_device, HUSB320_PORTROLE_REG, &role);
  if (ret == ESP_OK)
    ret = read_reg(s_device, HUSB320_CONTROL1_REG, &control);
  if (ret == ESP_OK &&
      ((role & HUSB320_ROLE_MASK) != HUSB320_ROLE_SINK || !(control & HUSB320_ENABLE)))
    ret = ESP_ERR_INVALID_STATE;
  if (ret == ESP_OK)
    ret = read_reg(s_device, HUSB320_STATUS_REG, &status);
  if (ret == ESP_OK)
    ret = read_reg(s_device, HUSB320_TYPE_REG, &type);
  if (ret == ESP_OK)
    ret = read_reg(s_device, HUSB320_STATUS_REG, &check);
  if (ret == ESP_OK && status != check)
    ret = ESP_ERR_INVALID_RESPONSE;
  if (ret == ESP_OK && (status & HUSB320_STATUS_ATTACH)) {
    uint8_t orientation = status & HUSB320_ORIENT_MASK;
    uint8_t level = status & HUSB320_LEVEL_MASK;
    if (!(status & HUSB320_STATUS_VBUS) || (status & HUSB320_STATUS_SAFE0V) ||
        type != HUSB320_TYPE_SINK ||
        (orientation != HUSB320_ORIENT_CC1 && orientation != HUSB320_ORIENT_CC2) || level == 0) {
      ret = ESP_ERR_INVALID_RESPONSE;
    } else {
      out_source->is_attached = true;
      out_source->advertised_ma = level == HUSB320_LEVEL_1500   ? HUSB320_CURRENT_1500_MA
                                  : level == HUSB320_LEVEL_3000 ? HUSB320_CURRENT_3000_MA
                                                                : 0;
    }
  }
cleanup:
  if (ret != ESP_OK)
    out_source->last_reg = s_last_reg;
  xSemaphoreGive(s_lock);
  return ret;
}

bool husb320_host_mode(void) {
  return atomic_load(&s_host_mode);
}

esp_err_t husb320_set_host_mode(bool enabled) {
  if (s_lock == NULL || s_device == NULL)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(HUSB320_TIMEOUT_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  uint8_t control = 0, role = 0, status = 0, current = 0, checked = 0;
  esp_err_t ret = read_reg(s_device, HUSB320_STATUS_REG, &status);
  if (ret == ESP_OK && enabled && (status & HUSB320_STATUS_VBUS))
    ret = ESP_ERR_INVALID_STATE;
  if (ret == ESP_OK)
    ret = read_reg(s_device, HUSB320_CONTROL1_REG, &control);
  if (ret == ESP_OK)
    ret = read_reg(s_device, HUSB320_PORTROLE_REG, &role);
  if (ret == ESP_OK) {
    // Keep automatic sink recovery out of the transaction, including failed writes.
    atomic_store(&s_host_mode, true);
    ret = write_reg(s_device, HUSB320_CONTROL1_REG, control & ~HUSB320_ENABLE);
  }
  if (ret == ESP_OK && enabled) {
    ret = read_reg(s_device, HUSB320_CONTROL_REG, &current);
    if (ret == ESP_OK)
      ret = write_reg(s_device,
                      HUSB320_CONTROL_REG,
                      (current & ~HUSB320_HOST_CURRENT_MASK) | HUSB320_HOST_CURRENT_DEFAULT);
    if (ret == ESP_OK)
      ret = read_reg(s_device, HUSB320_CONTROL_REG, &checked);
    if (ret == ESP_OK && (checked & HUSB320_HOST_CURRENT_MASK) != HUSB320_HOST_CURRENT_DEFAULT)
      ret = ESP_ERR_INVALID_RESPONSE;
  }
  uint8_t requested = enabled ? HUSB320_ROLE_SOURCE : HUSB320_ROLE_SINK;
  if (ret == ESP_OK)
    ret = write_reg(s_device, HUSB320_PORTROLE_REG, (role & ~HUSB320_ROLE_MASK) | requested);
  if (ret == ESP_OK)
    ret = read_reg(s_device, HUSB320_PORTROLE_REG, &checked);
  if (ret == ESP_OK && (checked & HUSB320_ROLE_MASK) != requested)
    ret = ESP_ERR_INVALID_RESPONSE;
  if (ret == ESP_OK)
    ret = write_reg(s_device, HUSB320_CONTROL1_REG, control | HUSB320_ENABLE);
  if (ret == ESP_OK)
    ret = read_reg(s_device, HUSB320_CONTROL1_REG, &checked);
  if (ret == ESP_OK && !(checked & HUSB320_ENABLE))
    ret = ESP_ERR_INVALID_RESPONSE;
  if (ret == ESP_OK)
    atomic_store(&s_host_mode, enabled);
  xSemaphoreGive(s_lock);
  return ret;
}

esp_err_t husb320_host_attached(bool *out_attached) {
  if (out_attached == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_attached = false;
  if (!atomic_load(&s_host_mode) || s_lock == NULL)
    return ESP_ERR_INVALID_STATE;
  esp_err_t ret = ESP_ERR_TIMEOUT;
  for (uint32_t attempt = 0; attempt < HUSB320_HOST_READ_ATTEMPTS; ++attempt) {
    ret = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(HUSB320_TIMEOUT_MS)) == pdTRUE) {
      uint8_t status = 0, type = 0, role = 0, check = 0;
      ret = atomic_load(&s_host_mode) ? read_reg(s_device, HUSB320_PORTROLE_REG, &role)
                                      : ESP_ERR_INVALID_STATE;
      if (ret == ESP_OK)
        ret = read_reg(s_device, HUSB320_STATUS_REG, &status);
      if (ret == ESP_OK)
        ret = read_reg(s_device, HUSB320_TYPE_REG, &type);
      if (ret == ESP_OK)
        ret = read_reg(s_device, HUSB320_STATUS_REG, &check);
      if (ret == ESP_OK && (status != check || (role & HUSB320_ROLE_MASK) != HUSB320_ROLE_SOURCE))
        ret = ESP_ERR_INVALID_RESPONSE;
      if (ret == ESP_OK)
        *out_attached = (status & HUSB320_STATUS_ATTACH) &&
                        (type & ~HUSB320_TYPE_ACTIVE_CABLE) == HUSB320_TYPE_SOURCE;
      xSemaphoreGive(s_lock);
    }
    if (ret != ESP_ERR_TIMEOUT)
      return ret;
    // A busy shared I2C bus is not a detach; revalidate the entire sample once.
    if (attempt + 1 < HUSB320_HOST_READ_ATTEMPTS)
      vTaskDelay(pdMS_TO_TICKS(HUSB320_HOST_RETRY_MS));
  }
  return ret;
}

static esp_err_t read_reg(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *out) {
  s_last_reg = reg;
  return i2c_master_transmit_receive(device, &reg, 1, out, 1, HUSB320_TIMEOUT_MS);
}

static esp_err_t write_reg(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value) {
  s_last_reg = reg;
  const uint8_t data[] = {reg, value};
  return i2c_master_transmit(device, data, sizeof(data), HUSB320_TIMEOUT_MS);
}
