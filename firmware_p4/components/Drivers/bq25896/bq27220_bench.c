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

#include "bq27220_bench.h"
#include "power_profile.h"
#include <stdatomic.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_init.h"
#define GAUGE_ADDRESS           0x55
#define GAUGE_HZ                100000
#define GAUGE_TIMEOUT_MS        100
#define GAUGE_DELAY_MS          20
#define GAUGE_CONFIG_TIMEOUT_US 3000000
#define GAUGE_OP_STATUS         0x3A
#define GAUGE_CONFIG_BIT        0x0400
#define GAUGE_SECURITY_MASK     0x0006
#define GAUGE_SEALED            0x0006
#define GAUGE_INIT_BIT          0x0020
#define GAUGE_CONTROL           0x00
#define GAUGE_MAC               0x3E
#define GAUGE_MAC_CHECKSUM      0x60
#define GAUGE_MAC_BYTES         36
#define GAUGE_ENTER_CONFIG      0x0090
#define GAUGE_EXIT_CONFIG       0x0092
#define GAUGE_EXIT_REINIT       0x0091
#define GAUGE_DEVICE_NUMBER     0x0001
#define GAUGE_EXPECTED_ID       0x0220
#define GAUGE_FULL_CAPACITY     0x929D
#define GAUGE_DESIGN_CAPACITY   0x929F
#define GAUGE_PACK_CAPACITY     POWER_PROFILE_CAPACITY_MAH
#define GAUGE_MAX_MV            6000
#define GAUGE_MAX_SOC           100
static const char *TAG = "BQ27220_BENCH";
static atomic_flag s_busy = ATOMIC_FLAG_INIT;
static esp_err_t read_bytes(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t size) {
  return i2c_master_transmit_receive(dev, &reg, 1, data, size, GAUGE_TIMEOUT_MS);
}
static esp_err_t word(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t *value) {
  uint8_t data[2];
  esp_err_t err = read_bytes(dev, reg, data, sizeof(data));
  if (err == ESP_OK)
    *value = data[0] | ((uint16_t)data[1] << 8);
  return err;
}
static esp_err_t control(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t value) {
  uint8_t data[] = {reg, value & 0xff, value >> 8};
  esp_err_t err = i2c_master_transmit(dev, data, sizeof(data), GAUGE_TIMEOUT_MS);
  vTaskDelay(pdMS_TO_TICKS(GAUGE_DELAY_MS));
  return err;
}
static esp_err_t memory_read(i2c_master_dev_handle_t dev, uint16_t address, uint16_t *value) {
  esp_err_t err = control(dev, GAUGE_MAC, address);
  uint8_t data[GAUGE_MAC_BYTES] = {0};
  if (err == ESP_OK)
    err = read_bytes(dev, GAUGE_MAC, data, sizeof(data));
  if (err != ESP_OK)
    return err;
  uint8_t length = data[GAUGE_MAC_BYTES - 1], sum = 0;
  if (length < 6 || length > GAUGE_MAC_BYTES || data[0] != (address & 0xff) ||
      data[1] != (address >> 8))
    return ESP_ERR_INVALID_RESPONSE;
  for (uint8_t i = 0; i < length - 2; ++i)
    sum += data[i];
  if ((uint8_t)(0xff - sum) != data[GAUGE_MAC_BYTES - 2])
    return ESP_ERR_INVALID_CRC;
  *value = ((uint16_t)data[2] << 8) | data[3];
  return ESP_OK;
}
static esp_err_t memory_write(i2c_master_dev_handle_t dev, uint16_t address, uint16_t value) {
  uint8_t data[] = {GAUGE_MAC, address & 0xff, address >> 8, value >> 8, value & 0xff};
  uint8_t sum = 0;
  for (size_t i = 1; i < sizeof(data); ++i)
    sum += data[i];
  uint8_t check[] = {GAUGE_MAC_CHECKSUM, (uint8_t)(0xff - sum), 6};
  esp_err_t err = i2c_master_transmit(dev, data, sizeof(data), GAUGE_TIMEOUT_MS);
  vTaskDelay(pdMS_TO_TICKS(GAUGE_DELAY_MS));
  if (err == ESP_OK)
    err = i2c_master_transmit(dev, check, sizeof(check), GAUGE_TIMEOUT_MS);
  vTaskDelay(pdMS_TO_TICKS(GAUGE_DELAY_MS));
  uint16_t actual = 0;
  if (err == ESP_OK)
    err = memory_read(dev, address, &actual);
  if (err == ESP_OK && actual != value)
    err = ESP_ERR_INVALID_RESPONSE;
  ESP_LOGI(TAG,
           "DM write %04x requested=%u read=%u result=%s",
           address,
           value,
           actual,
           esp_err_to_name(err));
  return err;
}
static esp_err_t wait_mode(i2c_master_dev_handle_t dev, bool config) {
  int64_t end = esp_timer_get_time() + GAUGE_CONFIG_TIMEOUT_US;
  do {
    uint16_t status = 0;
    esp_err_t err = word(dev, GAUGE_OP_STATUS, &status);
    if (err != ESP_OK)
      return err;
    if (((status & GAUGE_CONFIG_BIT) != 0) == config)
      return ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(GAUGE_DELAY_MS));
  } while (esp_timer_get_time() < end);
  return ESP_ERR_TIMEOUT;
}

esp_err_t bq27220_read_sample(bq27220_sample_t *out_sample) {
  if (out_sample == NULL)
    return ESP_ERR_INVALID_ARG;
  if (atomic_flag_test_and_set(&s_busy))
    return ESP_ERR_INVALID_STATE;
  i2c_master_dev_handle_t dev = NULL;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  const i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                      .device_address = GAUGE_ADDRESS,
                                      .scl_speed_hz = GAUGE_HZ};
  static const uint8_t registers[] = {
      0x08, 0x0C, 0x14, 0x10, 0x12, 0x3C, 0x2C, 0x28, 0x0A, GAUGE_OP_STATUS};
  uint16_t words[sizeof(registers)] = {0};
  bq27220_sample_t sample = {0};
  if (i2c_get_bus() == NULL)
    goto cleanup;
  result = i2c_master_bus_add_device(i2c_get_bus(), &config, &dev);
  if (result != ESP_OK)
    goto cleanup;
  for (size_t i = 0; i < sizeof(registers); ++i) {
    result = word(dev, registers[i], &words[i]);
    if (result != ESP_OK)
      goto cleanup;
  }
  sample.voltage_mv = words[0];
  sample.current_ma = (int16_t)words[1];
  sample.average_current_ma = (int16_t)words[2];
  sample.remaining_mah = words[3];
  sample.full_mah = words[4];
  sample.design_mah = words[5];
  sample.soc_pct = words[6];
  sample.internal_temperature_dk = words[7];
  sample.battery_status = words[8];
  sample.operation_status = words[9];
  if (!(sample.operation_status & GAUGE_INIT_BIT) || (sample.operation_status & GAUGE_CONFIG_BIT) ||
      !sample.voltage_mv || sample.voltage_mv > GAUGE_MAX_MV || sample.soc_pct > GAUGE_MAX_SOC ||
      !sample.full_mah)
    result = ESP_ERR_INVALID_RESPONSE;
cleanup:
  if (dev != NULL) {
    esp_err_t removed = i2c_master_bus_rm_device(dev);
    if (result == ESP_OK)
      result = removed;
  }
  if (result == ESP_OK)
    *out_sample = sample;
  atomic_flag_clear(&s_busy);
  return result;
}

esp_err_t bq27220_bench(bool apply_capacity) {
  if (atomic_flag_test_and_set(&s_busy))
    return ESP_ERR_INVALID_STATE;
  i2c_master_dev_handle_t dev = NULL;
  esp_err_t err = ESP_OK;
  bool entered = false, changed = false;
  uint16_t old[2] = {0}, status = 0, id = 0;
  const uint16_t addresses[] = {GAUGE_FULL_CAPACITY, GAUGE_DESIGN_CAPACITY};
  const i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                   .device_address = GAUGE_ADDRESS,
                                   .scl_speed_hz = GAUGE_HZ};
  if (i2c_get_bus() == NULL) {
    err = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  err = i2c_master_bus_add_device(i2c_get_bus(), &cfg, &dev);
  if (err != ESP_OK)
    goto cleanup;
  err = control(dev, GAUGE_CONTROL, GAUGE_DEVICE_NUMBER);
  if (err == ESP_OK)
    err = word(dev, 0x40, &id);
  if (err == ESP_OK && id != GAUGE_EXPECTED_ID)
    err = ESP_ERR_INVALID_RESPONSE;
  if (err == ESP_OK)
    err = word(dev, GAUGE_OP_STATUS, &status);
  ESP_LOGI(TAG,
           "ID=%04x status=%04x security=%u apply=%u",
           id,
           status,
           (status >> 1) & 3,
           apply_capacity);
  if (err != ESP_OK)
    goto cleanup;
  if ((status & GAUGE_SECURITY_MASK) == GAUGE_SEALED || !(status & GAUGE_INIT_BIT) ||
      (status & GAUGE_CONFIG_BIT)) {
    err = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  static const uint16_t inspect[] = {
      0x9206, 0x9208, 0x929A, 0x929B, 0x929D, 0x929F, 0x92A3, 0x91FB, 0x91FD, 0x9201};
  for (size_t i = 0; i < sizeof(inspect) / sizeof(inspect[0]); ++i) {
    uint16_t value = 0;
    err = memory_read(dev, inspect[i], &value);
    ESP_LOGI(TAG, "DM %04x=%04x (%u) %s", inspect[i], value, value, esp_err_to_name(err));
    if (err != ESP_OK)
      goto cleanup;
    if (inspect[i] == 0x929A && (value >> 8) != 0) {
      err = ESP_ERR_NOT_SUPPORTED;
      goto cleanup;
    }
  }
  if (!apply_capacity)
    goto cleanup;
  for (size_t i = 0; i < 2; ++i) {
    err = memory_read(dev, addresses[i], &old[i]);
    if (err != ESP_OK)
      goto cleanup;
  }
  if (old[1] == GAUGE_PACK_CAPACITY) {
    ESP_LOGI(TAG, "Nominal capacity already configured; preserve learned FCC; no reinitialization");
    goto cleanup;
  }
  /* Mark before transmission: cleanup also handles an ACK lost after acceptance. */
  entered = true;
  err = control(dev, GAUGE_CONTROL, GAUGE_ENTER_CONFIG);
  if (err == ESP_OK)
    err = wait_mode(dev, true);
  if (err != ESP_OK)
    goto cleanup;
  for (size_t i = 0; i < 2; ++i) {
    changed = true;
    err = memory_write(dev, addresses[i], GAUGE_PACK_CAPACITY);
    if (err != ESP_OK)
      break;
  }
  if (err != ESP_OK && changed) {
    for (size_t i = 0; i < 2; ++i) {
      esp_err_t rollback = memory_write(dev, addresses[i], old[i]);
      ESP_LOGW(TAG, "rollback %04x=%u result=%s", addresses[i], old[i], esp_err_to_name(rollback));
    }
  }
cleanup:
  if (entered) {
    esp_err_t exit_err =
        control(dev, GAUGE_CONTROL, err == ESP_OK ? GAUGE_EXIT_REINIT : GAUGE_EXIT_CONFIG);
    if (exit_err == ESP_OK)
      exit_err = wait_mode(dev, false);
    ESP_LOGI(TAG, "Exit config=%s", esp_err_to_name(exit_err));
    if (err == ESP_OK)
      err = exit_err;
    if (err == ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      for (size_t i = 0; i < 2; ++i) {
        uint16_t value = 0;
        esp_err_t read_err = memory_read(dev, addresses[i], &value);
        if (read_err == ESP_OK && value != GAUGE_PACK_CAPACITY)
          read_err = ESP_ERR_INVALID_RESPONSE;
        ESP_LOGI(TAG, "final %04x=%u %s", addresses[i], value, esp_err_to_name(read_err));
        if (read_err != ESP_OK)
          err = read_err;
      }
    }
  }
  if (dev) {
    esp_err_t removed = i2c_master_bus_rm_device(dev);
    if (err == ESP_OK)
      err = removed;
  }
  ESP_LOGW(
      TAG,
      "Capacity-only RAM profile, no OTP/calibration/chemistry writes; SOC remains unqualified");
  atomic_flag_clear(&s_busy);
  return err;
}
