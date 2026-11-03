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

#include "aw8623.h"

#include <stdatomic.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c_init.h"

#define AW8623_ADDRESS               0x5A
#define AW8623_ID_REG                0x00
#define AW8623_CHIP_ID               0x23
#define AW8623_STATUS_REG            0x01
#define AW8623_INTERRUPT_REG         0x02
#define AW8623_I2C_TIMEOUT_MS        100
#define AW8623_RESET_SETTLE_MS       10
#define AW8623_SUPPLY_CEILING_MV     5500
#define AW8623_SAMPLE_MAX            127
#define AW8623_SAMPLES_PER_MS        12
#define AW8623_RAM_BASE              0x0800
#define AW8623_RAM_HEADER_BYTES      5
#define AW8623_RAM_CHUNK             64
#define AW8623_SYSCTRL               0x04
#define AW8623_GO                    0x05
#define AW8623_RAMDATA               0x42
#define AW8623_RAMADDRH              0x40
#define AW8623_RAMADDRL              0x41
#define AW8623_STANDBY               0x41
#define AW8623_RAM_INIT              0x61
#define AW8623_RAM_ACTIVE            0x40
#define AW8623_FAULT_MASK            0x66
#define AW8623_POLL_MS               10
#define AW8623_COMPLETION_MARGIN_MS  100
#define AW8623_US_PER_MS             1000
#define AW8623_BYTE_BITS             8
#define AW8623_BYTE_MASK             0xff
#define AW8623_SRAM_BYTES            0x2000
#define AW8623_TRIGGER_CFG_REG       0x1C
#define AW8623_TRIGGER_DISABLED      0x40
#define AW8623_WAVE_CTRL_REG         0x31
#define AW8623_ERM_NO_OVERDRIVE      0x00
#define AW8623_BRAKE_CFG_REG         0x39
#define AW8623_BRAKE_DISABLED        0x00
#define AW8623_BRAKE1_REG            0x33
#define AW8623_BRAKE2_REG            0x34
#define AW8623_BRAKE_COUNT_REG       0x35
#define AW8623_DATA_CTRL_REG         0x2B
#define AW8623_FILTER_DISABLED       0x40
#define AW8623_PWM_CFG_REG           0x2E
#define AW8623_SAMPLE_RATE_12K       0xE1
#define AW8623_GAIN_REG              0x3B
#define AW8623_UNITY_GAIN            0x80
#define AW8623_ADC_CFG_REG           0x66
#define AW8623_SOFTWARE_COMPENSATION 0x00
#define AW8623_PWM_PROTECT_REG       0x2D
#define AW8623_DC_PROTECT_DISABLED   0x20
#define AW8623_PROTECT_LEVEL_REG     0x3E
#define AW8623_DC_PROTECT_LEVEL      0x3F
#define AW8623_BASE_HIGH_REG         0x21
#define AW8623_BASE_HIGH_VALUE       0x08
#define AW8623_BASE_LOW_REG          0x22
#define AW8623_BASE_LOW_VALUE        0x00
#define AW8623_SEQUENCE1_REG         0x07
#define AW8623_WAVE_ID               0x01
#define AW8623_SEQUENCE2_REG         0x08
#define AW8623_SEQUENCE_END          0x00
#define AW8623_WAVE_LOOP_REG         0x0F
#define AW8623_NO_LOOP               0x00
#define AW8623_MAIN_LOOP_REG         0x13
#define AW8623_INTERRUPT_MASK_REG    0x03
#define AW8623_FAULT_DONE_UNMASK     0x18

_Static_assert(AW8623_RAM_BASE + AW8623_RAM_HEADER_BYTES +
                       AW8623_MAX_DURATION_MS * AW8623_SAMPLES_PER_MS <=
                   AW8623_SRAM_BYTES,
               "waveform SRAM capacity");

static const char *TAG = "AW8623";
static atomic_flag s_busy = ATOMIC_FLAG_INIT;
static i2c_master_dev_handle_t s_device;

static esp_err_t read_register(uint8_t reg, uint8_t *out_value);
static esp_err_t
check_epoch(const board_control_ops_t *controls, uint64_t epoch, aw8623_diagnostic_t *out_result);

esp_err_t aw8623_stop(const board_control_ops_t *controls) {
  if (xPortInIsrContext())
    return ESP_ERR_INVALID_STATE;
  if (atomic_flag_test_and_set(&s_busy))
    return ESP_ERR_INVALID_STATE;
  esp_err_t result = board_control_apply(controls, BOARD_CONTROL_HAPTIC, false, NULL);
  atomic_flag_clear(&s_busy);
  return result;
}

esp_err_t aw8623_probe(const board_control_ops_t *controls, aw8623_diagnostic_t *out_result) {
  if (out_result == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out_result, 0, sizeof(*out_result));
  if (xPortInIsrContext())
    return ESP_ERR_INVALID_STATE;
  if (atomic_flag_test_and_set(&s_busy))
    return ESP_ERR_INVALID_STATE;
  uint64_t epoch = 0;
  esp_err_t result = board_control_apply(controls, BOARD_CONTROL_HAPTIC, false, NULL);
  if (result != ESP_OK)
    goto cleanup;
  vTaskDelay(pdMS_TO_TICKS(AW8623_RESET_SETTLE_MS));
  i2c_master_bus_handle_t bus = i2c_get_bus();
  if (bus == NULL) {
    result = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  if (s_device == NULL) {
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AW8623_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    result = i2c_master_bus_add_device(bus, &config, &s_device);
    if (result != ESP_OK)
      goto cleanup;
  }
  result = board_control_apply(controls, BOARD_CONTROL_HAPTIC, true, &epoch);
  if (result != ESP_OK)
    goto cleanup;
  vTaskDelay(pdMS_TO_TICKS(AW8623_RESET_SETTLE_MS));
  result = check_epoch(controls, epoch, out_result);
  if (result != ESP_OK)
    goto cleanup;
  result = read_register(AW8623_ID_REG, &out_result->chip_id);
  if (result != ESP_OK)
    goto cleanup;
  if (out_result->chip_id != AW8623_CHIP_ID) {
    result = ESP_ERR_NOT_SUPPORTED;
    goto cleanup;
  }
  out_result->is_identified = true;
  result = read_register(AW8623_STATUS_REG, &out_result->system_status);
  if (result == ESP_OK)
    result = read_register(AW8623_INTERRUPT_REG, &out_result->interrupts);
  if (result == ESP_OK)
    result = check_epoch(controls, epoch, out_result);
cleanup: {
  esp_err_t reset = board_control_apply(controls, BOARD_CONTROL_HAPTIC, false, NULL);
  out_result->is_reset_confirmed = reset == ESP_OK;
  if (reset != ESP_OK)
    result = reset;
  if (s_device != NULL) {
    esp_err_t release = i2c_master_bus_rm_device(s_device);
    if (release == ESP_OK)
      s_device = NULL;
    if (result == ESP_OK)
      result = release;
  }
}
  if (result != ESP_OK)
    ESP_LOGW(TAG, "Diagnostic failed: %s", esp_err_to_name(result));
  else
    ESP_LOGI(TAG,
             "AW8623 identified; SYSST=0x%02x SYSINT=0x%02x; reset asserted",
             out_result->system_status,
             out_result->interrupts);
  atomic_flag_clear(&s_busy);
  return result;
}

static esp_err_t write_register(uint8_t reg, uint8_t value) {
  const uint8_t bytes[] = {reg, value};
  return i2c_master_transmit(s_device, bytes, sizeof(bytes), AW8623_I2C_TIMEOUT_MS);
}

static esp_err_t load_waveform(const aw8623_playback_t *request) {
  // Conservative full-scale model: no VBAT compensation and no gain above unity.
  uint8_t amplitude = (uint32_t)AW8623_SAMPLE_MAX * AW8623_NOMINAL_MV * request->intensity /
                      (AW8623_SUPPLY_CEILING_MV * AW8623_INTENSITY_MAX);
  uint16_t samples = request->duration_ms * AW8623_SAMPLES_PER_MS;
  uint16_t start = AW8623_RAM_BASE + AW8623_RAM_HEADER_BYTES;
  uint16_t end = start + samples - 1;
  const uint8_t header[] = {AW8623_RAMDATA,
                            1,
                            start >> AW8623_BYTE_BITS,
                            start & AW8623_BYTE_MASK,
                            end >> AW8623_BYTE_BITS,
                            end & AW8623_BYTE_MASK};
  esp_err_t result = write_register(AW8623_SYSCTRL, AW8623_RAM_INIT);
  if (result == ESP_OK)
    result = write_register(AW8623_RAMADDRH, AW8623_RAM_BASE >> AW8623_BYTE_BITS);
  if (result == ESP_OK)
    result = write_register(AW8623_RAMADDRL, AW8623_RAM_BASE & AW8623_BYTE_MASK);
  if (result == ESP_OK)
    result = i2c_master_transmit(s_device, header, sizeof(header), AW8623_I2C_TIMEOUT_MS);
  uint8_t chunk[AW8623_RAM_CHUNK + 1];
  chunk[0] = AW8623_RAMDATA;
  memset(chunk + 1, amplitude, AW8623_RAM_CHUNK);
  while (result == ESP_OK && samples != 0) {
    if (request->is_cancelled != NULL && request->is_cancelled())
      return ESP_ERR_INVALID_STATE;
    uint16_t count = samples < AW8623_RAM_CHUNK ? samples : AW8623_RAM_CHUNK;
    result = i2c_master_transmit(s_device, chunk, count + 1, AW8623_I2C_TIMEOUT_MS);
    samples -= count;
  }
  if (result == ESP_OK)
    result = write_register(AW8623_SYSCTRL, AW8623_STANDBY);
  return result;
}

esp_err_t aw8623_play(const board_control_ops_t *controls, const aw8623_playback_t *request) {
  if (request == NULL || request->intensity == 0 || request->intensity > AW8623_INTENSITY_MAX ||
      request->duration_ms == 0 || request->duration_ms > AW8623_MAX_DURATION_MS)
    return ESP_ERR_INVALID_ARG;
  if (xPortInIsrContext())
    return ESP_ERR_INVALID_STATE;
  if (atomic_flag_test_and_set(&s_busy))
    return ESP_ERR_INVALID_STATE;
  aw8623_diagnostic_t diagnostic = {0};
  uint64_t epoch = 0;
  esp_err_t result = board_control_apply(controls, BOARD_CONTROL_HAPTIC, false, NULL);
  if (result != ESP_OK)
    goto cleanup;
  vTaskDelay(pdMS_TO_TICKS(AW8623_RESET_SETTLE_MS));
  if (s_device == NULL) {
    i2c_master_bus_handle_t bus = i2c_get_bus();
    if (bus == NULL) {
      result = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AW8623_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    result = i2c_master_bus_add_device(bus, &config, &s_device);
    if (result != ESP_OK)
      goto cleanup;
  }
  if (request->is_cancelled != NULL && request->is_cancelled()) {
    result = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  result = board_control_apply(controls, BOARD_CONTROL_HAPTIC, true, &epoch);
  if (result != ESP_OK)
    goto cleanup;
  vTaskDelay(pdMS_TO_TICKS(AW8623_RESET_SETTLE_MS));
  result = read_register(AW8623_ID_REG, &diagnostic.chip_id);
  if (result != ESP_OK)
    goto cleanup;
  if (diagnostic.chip_id != AW8623_CHIP_ID) {
    result = ESP_ERR_NOT_SUPPORTED;
    goto cleanup;
  }
  // ERM, finite RAM sequence, 12 ksample/s, no trigger, brake, overdrive or compensation.
  static const uint8_t CONFIG[][2] = {
      {AW8623_TRIGGER_CFG_REG, AW8623_TRIGGER_DISABLED},
      {AW8623_WAVE_CTRL_REG, AW8623_ERM_NO_OVERDRIVE},
      {AW8623_BRAKE_CFG_REG, AW8623_BRAKE_DISABLED},
      {AW8623_BRAKE1_REG, AW8623_BRAKE_DISABLED},
      {AW8623_BRAKE2_REG, AW8623_BRAKE_DISABLED},
      {AW8623_BRAKE_COUNT_REG, AW8623_BRAKE_DISABLED},
      {AW8623_DATA_CTRL_REG, AW8623_FILTER_DISABLED},
      {AW8623_PWM_CFG_REG, AW8623_SAMPLE_RATE_12K},
      {AW8623_GAIN_REG, AW8623_UNITY_GAIN},
      {AW8623_ADC_CFG_REG, AW8623_SOFTWARE_COMPENSATION},
      {AW8623_PWM_PROTECT_REG, AW8623_DC_PROTECT_DISABLED},
      {AW8623_PROTECT_LEVEL_REG, AW8623_DC_PROTECT_LEVEL},
      {AW8623_BASE_HIGH_REG, AW8623_BASE_HIGH_VALUE},
      {AW8623_BASE_LOW_REG, AW8623_BASE_LOW_VALUE},
      {AW8623_SEQUENCE1_REG, AW8623_WAVE_ID},
      {AW8623_SEQUENCE2_REG, AW8623_SEQUENCE_END},
      {AW8623_WAVE_LOOP_REG, AW8623_NO_LOOP},
      {AW8623_MAIN_LOOP_REG, AW8623_NO_LOOP},
      {AW8623_INTERRUPT_MASK_REG, AW8623_FAULT_DONE_UNMASK},
  };
  result = check_epoch(controls, epoch, &diagnostic);
  for (size_t i = 0; result == ESP_OK && i < sizeof(CONFIG) / sizeof(CONFIG[0]); i++)
    result = write_register(CONFIG[i][0], CONFIG[i][1]);
  if (result == ESP_OK)
    result = load_waveform(request);
  if (result == ESP_OK)
    result = read_register(AW8623_INTERRUPT_REG, &diagnostic.interrupts);
  if (result == ESP_OK)
    result = read_register(AW8623_STATUS_REG, &diagnostic.system_status);
  if (result == ESP_OK && (diagnostic.system_status & AW8623_FAULT_MASK))
    result = ESP_FAIL;
  if (result == ESP_OK)
    result = check_epoch(controls, epoch, &diagnostic);
  if (result != ESP_OK)
    goto cleanup;
  if (request->is_cancelled != NULL && request->is_cancelled()) {
    result = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  result = write_register(AW8623_SYSCTRL, AW8623_RAM_ACTIVE);
  if (result != ESP_OK)
    goto cleanup;
  // An uncertain GO acknowledgement is never retried.
  result = write_register(AW8623_GO, 1);
  if (result != ESP_OK)
    goto cleanup;
  int64_t deadline = esp_timer_get_time() +
                     (request->duration_ms + AW8623_COMPLETION_MARGIN_MS) * AW8623_US_PER_MS;
  while (true) {
    if (request->is_cancelled != NULL && request->is_cancelled()) {
      result = ESP_ERR_INVALID_STATE;
      break;
    }
    result = check_epoch(controls, epoch, &diagnostic);
    if (result == ESP_OK)
      result = read_register(AW8623_INTERRUPT_REG, &diagnostic.interrupts);
    if (result == ESP_OK)
      result = read_register(AW8623_STATUS_REG, &diagnostic.system_status);
    if (result != ESP_OK)
      break;
    if ((diagnostic.interrupts | diagnostic.system_status) & AW8623_FAULT_MASK) {
      result = ESP_FAIL;
      break;
    }
    uint8_t go;
    result = read_register(AW8623_GO, &go);
    if (result != ESP_OK || !(go & 1))
      break;
    if (esp_timer_get_time() >= deadline) {
      result = ESP_ERR_TIMEOUT;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(AW8623_POLL_MS));
  }
cleanup: {
  // Reset also stops an operation whose GO write succeeded but ACK was lost.
  esp_err_t reset = board_control_apply(controls, BOARD_CONTROL_HAPTIC, false, NULL);
  if (reset != ESP_OK)
    result = reset;
  if (s_device != NULL) {
    esp_err_t release = i2c_master_bus_rm_device(s_device);
    if (release == ESP_OK)
      s_device = NULL;
    if (result == ESP_OK)
      result = release;
  }
}
  if (result != ESP_OK)
    ESP_LOGW(TAG, "Playback ended: %s", esp_err_to_name(result));
  atomic_flag_clear(&s_busy);
  return result;
}

static esp_err_t read_register(uint8_t reg, uint8_t *out_value) {
  return i2c_master_transmit_receive(s_device, &reg, 1, out_value, 1, AW8623_I2C_TIMEOUT_MS);
}

static esp_err_t
check_epoch(const board_control_ops_t *controls, uint64_t epoch, aw8623_diagnostic_t *out_result) {
  board_control_status_t status;
  esp_err_t result = controls->get_status(&status);
  if (result != ESP_OK)
    return result;
  if (!status.is_compatible || status.epoch != epoch ||
      !status.outputs[BOARD_CONTROL_HAPTIC].is_known ||
      !status.outputs[BOARD_CONTROL_HAPTIC].confirmed)
    return ESP_ERR_INVALID_STATE;
  out_result->has_irq_event |= status.is_haptic_irq_pending;
  return ESP_OK;
}
