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

#include "bq27220.h"
#include <stdatomic.h>
#include <stdint.h>
#include "i2c_init.h"
#define GAUGE_ADDRESS    0x55
#define GAUGE_HZ         100000
#define GAUGE_TIMEOUT_MS 100
#define GAUGE_OP_STATUS  0x3A
#define GAUGE_CONFIG_BIT 0x0400
#define GAUGE_INIT_BIT   0x0020
#define GAUGE_MAX_MV     6000
#define GAUGE_MAX_SOC    100
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
