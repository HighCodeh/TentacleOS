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

#include "sx1262.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sx1262_regs.h"
#include "sx1262_cmd.h"
#include "sx1262_fsm.h"
#include "sx1262_irq.h"
#include "sx1262_radio.h"

static const char *TAG = "SX1262";

static sx1262_config_t s_config;
static sx1262_callbacks_t s_callbacks = {0};
static bool s_is_running = false;
static bool s_is_initialized = false;
static TaskHandle_t s_irq_task_handle = NULL;
static TaskHandle_t s_stop_caller_handle = NULL;

#define SX1262_IRQ_TASK_STACK 4096
#define SX1262_IRQ_TASK_PRIO  10
#define SX1262_IRQ_TASK_CORE  0

static esp_err_t validate_hal(const sx1262_hal_t *hal);
static esp_err_t validate_config(const sx1262_config_t *config);
static esp_err_t hw_reset(sx1262_hal_t *hal);
static esp_err_t apply_workaround_w2(sx1262_hal_t *hal);
static esp_err_t apply_workaround_w4(sx1262_hal_t *hal, bool is_inverted_iq);
static esp_err_t calibrate_image(sx1262_hal_t *hal, uint32_t freq_hz);
static uint8_t compute_ldro(uint8_t sf, uint8_t bw);
static void irq_task(void *arg);

esp_err_t sx1262_init(const sx1262_config_t *config) {
  if (config == NULL) {
    ESP_LOGE(TAG, "config is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = validate_hal(&config->hal);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = validate_config(config);
  if (ret != ESP_OK) {
    return ret;
  }

  memcpy(&s_config, config, sizeof(sx1262_config_t));
  sx1262_hal_t *hal = &s_config.hal;

  ret = hw_reset(hal);
  if (ret != ESP_OK) {
    return ret;
  }

  sx1262_fsm_init(SX1262_STATE_STDBY_RC);

  uint8_t stdby_param = SX1262_STDBY_RC;
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_STANDBY, &stdby_param, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetStandby failed");
    return ret;
  }

  uint8_t reg_mode = SX1262_REGULATOR_DCDC;
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_REGULATOR_MODE, &reg_mode, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetRegulatorMode failed");
    return ret;
  }

  uint8_t cal_param = SX1262_CALIBRATE_ALL;
  ret = sx1262_cmd_write(hal, SX1262_OP_CALIBRATE, &cal_param, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Calibrate failed");
    return ret;
  }

  ret = sx1262_cmd_wait_busy(hal, SX1262_WAIT_BUSY_TIMEOUT_MS);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BUSY timeout after Calibrate");
    return ret;
  }

  ret = calibrate_image(hal, config->frequency_hz);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = apply_workaround_w2(hal);
  if (ret != ESP_OK) {
    return ret;
  }

  uint8_t fallback_param = SX1262_FALLBACK_STDBY_RC;
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_RX_TX_FALLBACK_MODE, &fallback_param, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetRxTxFallbackMode failed");
    return ret;
  }

  uint8_t dio2_param = 0x01;
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_DIO2_AS_RF_SWITCH, &dio2_param, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetDIO2AsRfSwitch failed");
    return ret;
  }

  uint8_t err_buf[2] = {0};
  ret = sx1262_cmd_read(hal, SX1262_OP_GET_DEVICE_ERRORS, err_buf, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GetDeviceErrors failed");
    return ret;
  }
  uint16_t device_errors = ((uint16_t)err_buf[0] << 8) | err_buf[1];
  if (device_errors != 0) {
    ESP_LOGE(TAG, "Device errors after calibration: 0x%04X", device_errors);
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t clr_params[2] = {0x00, 0x00};
  sx1262_cmd_write(hal, SX1262_OP_CLEAR_DEVICE_ERRORS, clr_params, 2);

  s_is_initialized = true;

  ret = sx1262_config_lora(config);
  if (ret != ESP_OK) {
    return ret;
  }

  sx1262_irq_init(hal, &s_config, &s_callbacks);

  sx1262_radio_init(hal, &s_config);

  uint8_t status = 0;
  ret = sx1262_get_status(&status);
  if (ret != ESP_OK) {
    return ret;
  }

  uint8_t chip_mode = (status & SX1262_STATUS_CHIP_MODE_MASK) >> SX1262_STATUS_CHIP_MODE_SHIFT;

  ESP_LOGI(TAG,
           "Init OK — status: 0x%02X, chip_mode: %d (STDBY_RC=%d)",
           status,
           chip_mode,
           SX1262_CHIP_MODE_STDBY_RC);
  ESP_LOGI(TAG,
           "Initialized — freq: %lu, sf: %d, bw: 0x%02X, power: %d dBm",
           (unsigned long)config->frequency_hz,
           config->sf,
           config->bw,
           config->tx_power_dbm);

  return ESP_OK;
}

esp_err_t sx1262_config_lora(const sx1262_config_t *config) {
  if (config == NULL) {
    ESP_LOGE(TAG, "config_lora: config is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = validate_config(config);
  if (ret != ESP_OK) {
    return ret;
  }

  sx1262_hal_t hal_backup = s_config.hal;
  memcpy(&s_config, config, sizeof(sx1262_config_t));
  s_config.hal = hal_backup;

  sx1262_hal_t *hal = &s_config.hal;

  uint8_t pkt_type = SX1262_PACKET_TYPE_LORA;
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_PACKET_TYPE, &pkt_type, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetPacketType failed");
    return ret;
  }

  uint32_t rf_freq = (uint32_t)((uint64_t)config->frequency_hz * (1UL << 25) / 32000000UL);
  uint8_t freq_params[4] = {
      (uint8_t)(rf_freq >> 24),
      (uint8_t)(rf_freq >> 16),
      (uint8_t)(rf_freq >> 8),
      (uint8_t)(rf_freq),
  };
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_RF_FREQUENCY, freq_params, 4);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetRfFrequency failed");
    return ret;
  }

  uint8_t pa_params[4] = {
      SX1262_PA_DUTY_CYCLE_22DBM,
      SX1262_PA_HP_MAX_22DBM,
      SX1262_PA_DEVICE_SEL_SX1262,
      SX1262_PA_LUT_1,
  };
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_PA_CONFIG, pa_params, 4);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetPaConfig failed");
    return ret;
  }

  uint8_t tx_params[2] = {(uint8_t)config->tx_power_dbm, SX1262_PA_RAMP_200U};
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_TX_PARAMS, tx_params, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetTxParams failed");
    return ret;
  }

  uint8_t buf_addr[2] = {0x00, 0x00};
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_BUFFER_BASE_ADDR, buf_addr, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetBufferBaseAddress failed");
    return ret;
  }

  uint8_t ldro = compute_ldro(config->sf, config->bw);
  uint8_t mod_params[4] = {config->sf, config->bw, config->cr, ldro};
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_MODULATION_PARAMS, mod_params, 4);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetModulationParams failed");
    return ret;
  }

  uint8_t header_type =
      config->is_implicit_hdr ? SX1262_LORA_HEADER_IMPLICIT : SX1262_LORA_HEADER_EXPLICIT;
  uint8_t crc_on = config->is_crc_on ? 0x01 : 0x00;
  uint8_t invert_iq = config->is_inverted_iq ? 0x01 : 0x00;
  uint8_t pkt_params[6] = {
      (uint8_t)(config->preamble_len >> 8),
      (uint8_t)(config->preamble_len & 0xFF),
      header_type,
      0xFF,
      crc_on,
      invert_iq,
  };
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_PACKET_PARAMS, pkt_params, 6);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetPacketParams failed");
    return ret;
  }

  uint16_t irq_mask = SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_DONE | SX1262_IRQ_TIMEOUT |
                      SX1262_IRQ_CRC_ERR | SX1262_IRQ_HEADER_ERR | SX1262_IRQ_CAD_DONE |
                      SX1262_IRQ_CAD_DETECTED;
  uint8_t irq_params[8] = {
      (uint8_t)(irq_mask >> 8),
      (uint8_t)(irq_mask),
      (uint8_t)(irq_mask >> 8),
      (uint8_t)(irq_mask),
      0x00,
      0x00,
      0x00,
      0x00,
  };
  ret = sx1262_cmd_write(hal, SX1262_OP_SET_DIO_IRQ_PARAMS, irq_params, 8);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetDioIrqParams failed");
    return ret;
  }

  uint16_t sync_word =
      config->is_public_network ? SX1262_LORA_SYNC_WORD_PUBLIC : SX1262_LORA_SYNC_WORD_PRIVATE;
  uint8_t sw_data[2] = {(uint8_t)(sync_word >> 8), (uint8_t)(sync_word)};
  ret = sx1262_cmd_write_register(hal, SX1262_REG_LORA_SYNC_WORD_MSB, sw_data, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetSyncWord failed");
    return ret;
  }

  ret = apply_workaround_w4(hal, config->is_inverted_iq);
  if (ret != ESP_OK) {
    return ret;
  }

  ESP_LOGI(TAG,
           "LoRa configured — SF%d BW=0x%02X CR=4/%d LDRO=%d power=%ddBm %s",
           config->sf,
           config->bw,
           config->cr + 4,
           ldro,
           config->tx_power_dbm,
           config->is_inverted_iq ? "IQ_INV" : "IQ_STD");

  return ESP_OK;
}

sx1262_state_t sx1262_get_state(void) {
  return sx1262_fsm_get_state();
}

bool sx1262_is_running(void) {
  return s_is_running;
}

esp_err_t sx1262_get_status(uint8_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  sx1262_hal_t *hal = &s_config.hal;

  uint8_t tx[2] = {SX1262_OP_GET_STATUS, 0x00};
  uint8_t rx[2] = {0};

  esp_err_t ret = sx1262_cmd_wait_busy(hal, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  hal->lock(hal->ctx);
  hal->cs_low(hal->ctx);
  hal->spi_transfer(hal->ctx, tx, rx, 2);
  hal->cs_high(hal->ctx);
  hal->unlock(hal->ctx);

  *out_status = rx[1];
  return ESP_OK;
}

esp_err_t sx1262_get_device_errors(uint16_t *out_errors) {
  if (out_errors == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  sx1262_hal_t *hal = &s_config.hal;
  uint8_t buf[2] = {0};

  esp_err_t ret = sx1262_cmd_read(hal, SX1262_OP_GET_DEVICE_ERRORS, buf, 2);
  if (ret != ESP_OK) {
    return ret;
  }

  *out_errors = ((uint16_t)buf[0] << 8) | buf[1];
  return ESP_OK;
}

esp_err_t sx1262_start(void) {
  if (s_is_running) {
    ESP_LOGE(TAG, "Already running");
    return ESP_ERR_INVALID_STATE;
  }
  if (!s_is_initialized) {
    ESP_LOGE(TAG, "Not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  BaseType_t xret = xTaskCreatePinnedToCore(irq_task,
                                            "sx1262_irq",
                                            SX1262_IRQ_TASK_STACK,
                                            NULL,
                                            SX1262_IRQ_TASK_PRIO,
                                            &s_irq_task_handle,
                                            SX1262_IRQ_TASK_CORE);

  if (xret != pdPASS || s_irq_task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to create IRQ task");
    return ESP_ERR_NO_MEM;
  }

  s_is_running = true;
  ESP_LOGI(TAG, "IRQ task started");
  return ESP_OK;
}

void sx1262_stop(void) {
  if (!s_is_running) {
    return;
  }
  s_stop_caller_handle = xTaskGetCurrentTaskHandle();
  s_is_running = false;
  if (s_irq_task_handle != NULL) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    s_irq_task_handle = NULL;
  }
  s_stop_caller_handle = NULL;
  s_config.hal.set_antenna(s_config.hal.ctx, SX1262_ANT_OFF);
  ESP_LOGI(TAG, "Stopped");
}

esp_err_t sx1262_set_callbacks(const sx1262_callbacks_t *cbs) {
  if (cbs == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  memcpy(&s_callbacks, cbs, sizeof(sx1262_callbacks_t));
  if (s_is_initialized) {
    sx1262_irq_init(&s_config.hal, &s_config, &s_callbacks);
  }
  return ESP_OK;
}

esp_err_t sx1262_process_irq(void) {
  return sx1262_irq_process();
}

esp_err_t sx1262_get_packet(sx1262_packet_t *out_packet) {
  return sx1262_irq_get_packet(out_packet);
}

esp_err_t sx1262_get_rssi_inst(int16_t *out_rssi_dbm) {
  return sx1262_radio_get_rssi_inst(out_rssi_dbm);
}

esp_err_t sx1262_get_stats(sx1262_stats_t *out_stats) {
  return sx1262_radio_get_stats(out_stats);
}
esp_err_t sx1262_transmit(const uint8_t *data, uint8_t len, uint32_t timeout_ms) {
  return sx1262_radio_transmit(data, len, timeout_ms);
}
esp_err_t sx1262_receive_single(uint32_t timeout_ms) {
  return sx1262_radio_receive_single(timeout_ms);
}

esp_err_t sx1262_receive_continuous(void) {
  return sx1262_radio_receive_continuous();
}

void sx1262_stop_rx(void) {
  sx1262_radio_stop_rx();
}
esp_err_t sx1262_cad_start(void) {
  return sx1262_radio_cad_start();
}
esp_err_t sx1262_sleep(bool is_warm) {
  return sx1262_radio_sleep(is_warm);
}

esp_err_t sx1262_wakeup(void) {
  return sx1262_radio_wakeup();
}

esp_err_t sx1262_set_rx_duty_cycle(uint32_t rx_ms, uint32_t sleep_ms) {
  return sx1262_radio_set_rx_duty_cycle(rx_ms, sleep_ms);
}

static void irq_task(void *arg) {
  (void)arg;
  sx1262_hal_t *hal = &s_config.hal;

  ESP_LOGI(TAG, "IRQ task running");

  while (s_is_running) {
    sx1262_irq_process();
    hal->delay_ms(hal->ctx, 10);
  }

  ESP_LOGI(TAG, "IRQ task exiting");

  if (s_stop_caller_handle != NULL) {
    xTaskNotifyGive(s_stop_caller_handle);
  }
  vTaskDelete(NULL);
}

static esp_err_t validate_hal(const sx1262_hal_t *hal) {
  if (hal->spi_transfer == NULL) {
    ESP_LOGE(TAG, "HAL: spi_transfer is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->cs_low == NULL) {
    ESP_LOGE(TAG, "HAL: cs_low is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->cs_high == NULL) {
    ESP_LOGE(TAG, "HAL: cs_high is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->reset_write == NULL) {
    ESP_LOGE(TAG, "HAL: reset_write is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->busy_read == NULL) {
    ESP_LOGE(TAG, "HAL: busy_read is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->delay_ms == NULL) {
    ESP_LOGE(TAG, "HAL: delay_ms is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->get_tick_ms == NULL) {
    ESP_LOGE(TAG, "HAL: get_tick_ms is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->lock == NULL) {
    ESP_LOGE(TAG, "HAL: lock is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->unlock == NULL) {
    ESP_LOGE(TAG, "HAL: unlock is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->enter_critical == NULL) {
    ESP_LOGE(TAG, "HAL: enter_critical is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->exit_critical == NULL) {
    ESP_LOGE(TAG, "HAL: exit_critical is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->set_antenna == NULL) {
    ESP_LOGE(TAG, "HAL: set_antenna is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (hal->ctx == NULL) {
    ESP_LOGE(TAG, "HAL: ctx is NULL");
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

static esp_err_t validate_config(const sx1262_config_t *config) {
  if (config->frequency_hz < 150000000UL || config->frequency_hz > 960000000UL) {
    ESP_LOGE(TAG, "frequency_hz out of range: %lu", (unsigned long)config->frequency_hz);
    return ESP_ERR_INVALID_ARG;
  }

  if (config->sf < SX1262_LORA_SF5 || config->sf > SX1262_LORA_SF12) {
    ESP_LOGE(TAG, "sf out of range: %d", config->sf);
    return ESP_ERR_INVALID_ARG;
  }

  if (!sx1262_bw_is_valid(config->bw)) {
    ESP_LOGE(TAG, "bw invalid: 0x%02X", config->bw);
    return ESP_ERR_INVALID_ARG;
  }

  if (config->bw == SX1262_LORA_BW_500 && config->sf < SX1262_LORA_SF6) {
    ESP_LOGE(TAG, "BW_500 requires SF >= 6, got SF%d", config->sf);
    return ESP_ERR_INVALID_ARG;
  }

  if (config->cr < SX1262_LORA_CR_4_5 || config->cr > SX1262_LORA_CR_4_8) {
    ESP_LOGE(TAG, "cr out of range: %d", config->cr);
    return ESP_ERR_INVALID_ARG;
  }

  if (config->tx_power_dbm < -9 || config->tx_power_dbm > 22) {
    ESP_LOGE(TAG, "tx_power_dbm out of range: %d", config->tx_power_dbm);
    return ESP_ERR_INVALID_ARG;
  }

  if (config->preamble_len < 2) {
    ESP_LOGE(TAG, "preamble_len too short: %d", config->preamble_len);
    return ESP_ERR_INVALID_ARG;
  }

  if (config->sf == SX1262_LORA_SF6 && !config->is_implicit_hdr) {
    ESP_LOGE(TAG, "SF6 requires implicit header mode");
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

static esp_err_t hw_reset(sx1262_hal_t *hal) {
  hal->set_antenna(hal->ctx, SX1262_ANT_OFF);
  hal->reset_write(hal->ctx, 0);
  hal->delay_ms(hal->ctx, SX1262_RESET_HOLD_MS);
  hal->reset_write(hal->ctx, 1);
  hal->delay_ms(hal->ctx, SX1262_RESET_WAIT_MS);

  esp_err_t ret = sx1262_cmd_wait_busy(hal, SX1262_WAIT_BUSY_TIMEOUT_MS);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BUSY did not go LOW after reset");
    return ret;
  }

  ESP_LOGI(TAG, "Hardware reset complete");
  return ESP_OK;
}

static esp_err_t apply_workaround_w2(sx1262_hal_t *hal) {
  uint8_t val = 0;
  esp_err_t ret = sx1262_cmd_read_register(hal, SX1262_REG_TX_CLAMP_CONFIG, &val, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "W2: read 0x%04X failed", SX1262_REG_TX_CLAMP_CONFIG);
    return ret;
  }

  val |= 0x1E; /* Set bits 4:1 to 1111 */

  ret = sx1262_cmd_write_register(hal, SX1262_REG_TX_CLAMP_CONFIG, &val, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "W2: write 0x%04X failed", SX1262_REG_TX_CLAMP_CONFIG);
    return ret;
  }

  ESP_LOGD(TAG, "W2 applied: reg 0x%04X = 0x%02X", SX1262_REG_TX_CLAMP_CONFIG, val);
  return ESP_OK;
}

static esp_err_t apply_workaround_w4(sx1262_hal_t *hal, bool is_inverted_iq) {
  uint8_t val = 0;
  esp_err_t ret = sx1262_cmd_read_register(hal, SX1262_REG_IQ_POLARITY, &val, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "W4: read 0x%04X failed", SX1262_REG_IQ_POLARITY);
    return ret;
  }

  if (is_inverted_iq) {
    val &= ~(1U << 2);
  } else {
    val |= (1U << 2);
  }

  ret = sx1262_cmd_write_register(hal, SX1262_REG_IQ_POLARITY, &val, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "W4: write 0x%04X failed", SX1262_REG_IQ_POLARITY);
    return ret;
  }

  ESP_LOGD(TAG,
           "W4 applied: reg 0x%04X = 0x%02X (inverted=%d)",
           SX1262_REG_IQ_POLARITY,
           val,
           is_inverted_iq);
  return ESP_OK;
}

static esp_err_t calibrate_image(sx1262_hal_t *hal, uint32_t freq_hz) {
  uint8_t params[2];

  if (freq_hz >= 902000000UL) {
    params[0] = 0xE1;
    params[1] = 0xE9;
  } else if (freq_hz >= 863000000UL) {
    params[0] = 0xD7;
    params[1] = 0xDB;
  } else if (freq_hz >= 779000000UL) {
    params[0] = 0xC1;
    params[1] = 0xC5;
  } else if (freq_hz >= 470000000UL) {
    params[0] = 0x75;
    params[1] = 0x81;
  } else {
    params[0] = 0x6B;
    params[1] = 0x6F;
  }

  esp_err_t ret = sx1262_cmd_write(hal, SX1262_OP_CALIBRATE_IMAGE, params, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CalibrateImage failed");
    return ret;
  }

  ret = sx1262_cmd_wait_busy(hal, SX1262_WAIT_BUSY_TIMEOUT_MS);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "BUSY timeout after CalibrateImage");
    return ret;
  }

  return ESP_OK;
}

static uint8_t compute_ldro(uint8_t sf, uint8_t bw) {
  if (sf >= SX1262_LORA_SF11 && sx1262_bw_to_hz(bw) <= 125000) {
    return 0x01;
  }
  return 0x00;
}
