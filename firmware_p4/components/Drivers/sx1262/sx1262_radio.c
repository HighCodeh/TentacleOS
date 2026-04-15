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

#include "sx1262_radio.h"

#include <string.h>

#include "esp_log.h"

#include "sx1262_regs.h"
#include "sx1262_cmd.h"
#include "sx1262_fsm.h"

static const char *TAG = "SX1262_RADIO";

static sx1262_hal_t *s_hal = NULL;
static sx1262_config_t *s_config = NULL;
static bool s_needs_reinit = false;

static esp_err_t apply_workaround_w1(void);
static esp_err_t update_packet_len(uint8_t len);

void sx1262_radio_init(sx1262_hal_t *hal, sx1262_config_t *config) {
  s_hal = hal;
  s_config = config;
  s_needs_reinit = false;
}

esp_err_t sx1262_radio_transmit(const uint8_t *data, uint8_t len, uint32_t timeout_ms) {
  if (data == NULL) {
    ESP_LOGE(TAG, "transmit: data is NULL");
    return ESP_ERR_INVALID_ARG;
  }
  if (len == 0) {
    ESP_LOGE(TAG, "transmit: len is 0");
    return ESP_ERR_INVALID_ARG;
  }
  if (s_hal == NULL || s_config == NULL) {
    ESP_LOGE(TAG, "transmit: not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  if (s_needs_reinit) {
    ESP_LOGE(TAG, "transmit: re-init required after cold sleep");
    return ESP_ERR_INVALID_STATE;
  }

  sx1262_state_t current = sx1262_fsm_get_state();
  if (current != SX1262_STATE_STDBY_RC && current != SX1262_STATE_STDBY_XOSC) {
    esp_err_t ret = sx1262_fsm_transition(SX1262_STATE_STDBY_RC);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "transmit: cannot transition to STDBY_RC from state %d", current);
      return ret;
    }
    uint8_t stdby = SX1262_STDBY_RC;
    sx1262_cmd_write(s_hal, SX1262_OP_SET_STANDBY, &stdby, 1);
  }

  esp_err_t ret = apply_workaround_w1();
  if (ret != ESP_OK) {
    return ret;
  }

  ret = update_packet_len(len);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = sx1262_cmd_write_buffer(s_hal, 0x00, data, len);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "WriteBuffer failed");
    return ret;
  }

  uint8_t clr[2] = {(uint8_t)(SX1262_IRQ_ALL >> 8), (uint8_t)(SX1262_IRQ_ALL)};
  ret = sx1262_cmd_write(s_hal, SX1262_OP_CLEAR_IRQ_STATUS, clr, 2);
  if (ret != ESP_OK) {
    return ret;
  }

  s_hal->set_antenna(s_hal->ctx, SX1262_ANT_TX);

  ret = sx1262_fsm_transition(SX1262_STATE_TX);
  if (ret != ESP_OK) {
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    return ret;
  }

  uint32_t tout = (timeout_ms == 0) ? 0x000000 : timeout_ms * 64;
  uint8_t tx_params[3] = {
      (uint8_t)(tout >> 16),
      (uint8_t)(tout >> 8),
      (uint8_t)(tout),
  };
  ret = sx1262_cmd_write(s_hal, SX1262_OP_SET_TX, tx_params, 3);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetTx failed");
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    sx1262_fsm_on_irq_complete();
    return ret;
  }

  ESP_LOGI(TAG, "TX started: %d bytes, timeout=%lu ms", len, (unsigned long)timeout_ms);
  return ESP_OK;
}

esp_err_t sx1262_radio_receive_single(uint32_t timeout_ms) {
  if (s_hal == NULL || s_config == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_needs_reinit) {
    ESP_LOGE(TAG, "receive_single: re-init required after cold sleep");
    return ESP_ERR_INVALID_STATE;
  }

  sx1262_state_t current = sx1262_fsm_get_state();
  if (current != SX1262_STATE_STDBY_RC && current != SX1262_STATE_STDBY_XOSC) {
    esp_err_t ret = sx1262_fsm_transition(SX1262_STATE_STDBY_RC);
    if (ret != ESP_OK) {
      return ret;
    }
    uint8_t stdby = SX1262_STDBY_RC;
    sx1262_cmd_write(s_hal, SX1262_OP_SET_STANDBY, &stdby, 1);
  }

  uint8_t clr[2] = {(uint8_t)(SX1262_IRQ_ALL >> 8), (uint8_t)(SX1262_IRQ_ALL)};
  esp_err_t ret = sx1262_cmd_write(s_hal, SX1262_OP_CLEAR_IRQ_STATUS, clr, 2);
  if (ret != ESP_OK) {
    return ret;
  }

  s_hal->set_antenna(s_hal->ctx, SX1262_ANT_RX);

  /* FSM → RX */
  ret = sx1262_fsm_transition(SX1262_STATE_RX);
  if (ret != ESP_OK) {
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    return ret;
  }

  uint32_t tout = (timeout_ms == 0) ? 0x000000 : timeout_ms * 64;
  uint8_t rx_params[3] = {
      (uint8_t)(tout >> 16),
      (uint8_t)(tout >> 8),
      (uint8_t)(tout),
  };
  ret = sx1262_cmd_write(s_hal, SX1262_OP_SET_RX, rx_params, 3);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetRx failed");
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    sx1262_fsm_on_irq_complete();
    return ret;
  }

  ESP_LOGI(TAG, "RX single started, timeout=%lu ms", (unsigned long)timeout_ms);
  return ESP_OK;
}

esp_err_t sx1262_radio_receive_continuous(void) {
  if (s_hal == NULL || s_config == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_needs_reinit) {
    ESP_LOGE(TAG, "receive_continuous: re-init required after cold sleep");
    return ESP_ERR_INVALID_STATE;
  }

  sx1262_state_t current = sx1262_fsm_get_state();
  if (current != SX1262_STATE_STDBY_RC && current != SX1262_STATE_STDBY_XOSC) {
    esp_err_t ret = sx1262_fsm_transition(SX1262_STATE_STDBY_RC);
    if (ret != ESP_OK) {
      return ret;
    }
    uint8_t stdby = SX1262_STDBY_RC;
    sx1262_cmd_write(s_hal, SX1262_OP_SET_STANDBY, &stdby, 1);
  }

  uint8_t clr[2] = {(uint8_t)(SX1262_IRQ_ALL >> 8), (uint8_t)(SX1262_IRQ_ALL)};
  esp_err_t ret = sx1262_cmd_write(s_hal, SX1262_OP_CLEAR_IRQ_STATUS, clr, 2);
  if (ret != ESP_OK) {
    return ret;
  }

  s_hal->set_antenna(s_hal->ctx, SX1262_ANT_RX);

  ret = sx1262_fsm_transition(SX1262_STATE_RX);
  if (ret != ESP_OK) {
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    return ret;
  }

  uint8_t rx_params[3] = {0xFF, 0xFF, 0xFF};
  ret = sx1262_cmd_write(s_hal, SX1262_OP_SET_RX, rx_params, 3);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetRx continuous failed");
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    sx1262_fsm_on_irq_complete();
    return ret;
  }

  ESP_LOGI(TAG, "RX continuous started");
  return ESP_OK;
}

void sx1262_radio_stop_rx(void) {
  if (s_hal == NULL) {
    return;
  }

  uint8_t stdby = SX1262_STDBY_RC;
  sx1262_cmd_write(s_hal, SX1262_OP_SET_STANDBY, &stdby, 1);
  s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);

  if (sx1262_fsm_transition(SX1262_STATE_STDBY_RC) != ESP_OK) {
    sx1262_fsm_on_irq_complete();
  }

  ESP_LOGI(TAG, "RX stopped → STDBY_RC");
}

esp_err_t sx1262_radio_cad_start(void) {
  if (s_hal == NULL || s_config == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  sx1262_state_t current = sx1262_fsm_get_state();
  if (current == SX1262_STATE_TX || current == SX1262_STATE_UNKNOWN) {
    ESP_LOGE(TAG, "cad_start: invalid state %d", current);
    return ESP_ERR_INVALID_STATE;
  }
  if (current != SX1262_STATE_STDBY_RC && current != SX1262_STATE_STDBY_XOSC) {
    esp_err_t ret = sx1262_fsm_transition(SX1262_STATE_STDBY_RC);
    if (ret != ESP_OK) {
      return ret;
    }
    uint8_t stdby = SX1262_STDBY_RC;
    sx1262_cmd_write(s_hal, SX1262_OP_SET_STANDBY, &stdby, 1);
  }

  uint8_t cad_sym_num;
  uint8_t cad_det_peak;
  uint8_t cad_det_min;

  switch (s_config->sf) {
    case SX1262_LORA_SF5:
      cad_sym_num = 2;
      cad_det_peak = 22;
      cad_det_min = 10;
      break;
    case SX1262_LORA_SF6:
      cad_sym_num = 2;
      cad_det_peak = 22;
      cad_det_min = 10;
      break;
    case SX1262_LORA_SF7:
      cad_sym_num = 2;
      cad_det_peak = 22;
      cad_det_min = 10;
      break;
    case SX1262_LORA_SF8:
      cad_sym_num = 2;
      cad_det_peak = 22;
      cad_det_min = 10;
      break;
    case SX1262_LORA_SF9:
      cad_sym_num = 4;
      cad_det_peak = 23;
      cad_det_min = 10;
      break;
    case SX1262_LORA_SF10:
      cad_sym_num = 4;
      cad_det_peak = 24;
      cad_det_min = 10;
      break;
    case SX1262_LORA_SF11:
      cad_sym_num = 4;
      cad_det_peak = 25;
      cad_det_min = 10;
      break;
    case SX1262_LORA_SF12:
      cad_sym_num = 4;
      cad_det_peak = 28;
      cad_det_min = 10;
      break;
    default:
      cad_sym_num = 2;
      cad_det_peak = 22;
      cad_det_min = 10;
      break;
  }

  uint8_t cad_params[7] = {
      cad_sym_num,
      cad_det_peak,
      cad_det_min,
      0x00,
      0x00,
      0x00,
      0x00,
  };
  esp_err_t ret = sx1262_cmd_write(s_hal, SX1262_OP_SET_CAD_PARAMS, cad_params, 7);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetCadParams failed");
    return ret;
  }

  uint8_t clr[2] = {(uint8_t)(SX1262_IRQ_ALL >> 8), (uint8_t)(SX1262_IRQ_ALL)};
  sx1262_cmd_write(s_hal, SX1262_OP_CLEAR_IRQ_STATUS, clr, 2);

  s_hal->set_antenna(s_hal->ctx, SX1262_ANT_RX);

  ret = sx1262_cmd_write(s_hal, SX1262_OP_SET_CAD, NULL, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetCAD failed");
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    return ret;
  }

  ESP_LOGI(
      TAG, "CAD started: SF%d, symNum=%d, detPeak=%d", s_config->sf, cad_sym_num, cad_det_peak);
  return ESP_OK;
}

esp_err_t sx1262_radio_get_rssi_inst(int16_t *out_rssi_dbm) {
  if (out_rssi_dbm == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_hal == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t raw = 0;
  esp_err_t ret = sx1262_cmd_read(s_hal, SX1262_OP_GET_RSSI_INST, &raw, 1);
  if (ret != ESP_OK) {
    return ret;
  }

  *out_rssi_dbm = -(int16_t)(raw / 2);
  return ESP_OK;
}

esp_err_t sx1262_radio_get_stats(sx1262_stats_t *out_stats) {
  if (out_stats == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_hal == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t buf[6] = {0};
  esp_err_t ret = sx1262_cmd_read(s_hal, SX1262_OP_GET_STATS, buf, 6);
  if (ret != ESP_OK) {
    return ret;
  }

  out_stats->nb_pkt_received = ((uint16_t)buf[0] << 8) | buf[1];
  out_stats->nb_crc_error = ((uint16_t)buf[2] << 8) | buf[3];
  out_stats->nb_header_error = ((uint16_t)buf[4] << 8) | buf[5];

  return ESP_OK;
}

esp_err_t sx1262_radio_reset_stats(void) {
  if (s_hal == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t params[6] = {0};
  return sx1262_cmd_write(s_hal, SX1262_OP_RESET_STATS, params, 6);
}

esp_err_t sx1262_radio_sleep(bool is_warm) {
  if (s_hal == NULL || s_config == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = sx1262_fsm_transition(SX1262_STATE_SLEEP);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "sleep: cannot transition to SLEEP from state %d", sx1262_fsm_get_state());
    return ret;
  }

  s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);

  uint8_t sleep_cfg = is_warm ? SX1262_SLEEP_WARM_START : SX1262_SLEEP_COLD_START;
  ret = sx1262_cmd_write(s_hal, SX1262_OP_SET_SLEEP, &sleep_cfg, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetSleep failed");
    sx1262_fsm_on_irq_complete();
    return ret;
  }

  if (!is_warm) {
    s_needs_reinit = true;
  }

  ESP_LOGI(TAG, "Sleep entered (%s)", is_warm ? "warm" : "cold");
  return ESP_OK;
}

esp_err_t sx1262_radio_wakeup(void) {
  if (s_hal == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  sx1262_state_t current = sx1262_fsm_get_state();
  if (current != SX1262_STATE_SLEEP) {
    ESP_LOGE(TAG, "wakeup: not in SLEEP state (state=%d)", current);
    return ESP_ERR_INVALID_STATE;
  }

  s_hal->lock(s_hal->ctx);
  s_hal->cs_low(s_hal->ctx);
  s_hal->delay_ms(s_hal->ctx, 1);
  s_hal->cs_high(s_hal->ctx);
  s_hal->unlock(s_hal->ctx);

  esp_err_t ret = sx1262_cmd_wait_busy(s_hal, SX1262_WAIT_BUSY_TIMEOUT_MS);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "wakeup: BUSY timeout after NSS toggle");
    return ret;
  }

  uint8_t stdby = SX1262_STDBY_RC;
  ret = sx1262_cmd_write(s_hal, SX1262_OP_SET_STANDBY, &stdby, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "wakeup: SetStandby failed");
    return ret;
  }

  ret = sx1262_fsm_transition(SX1262_STATE_STDBY_RC);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "wakeup: FSM transition failed");
    return ret;
  }

  ESP_LOGI(TAG, "Wakeup complete → STDBY_RC%s", s_needs_reinit ? " (cold — re-init required)" : "");
  return ESP_OK;
}

esp_err_t sx1262_radio_set_rx_duty_cycle(uint32_t rx_ms, uint32_t sleep_ms) {
  if (s_hal == NULL || s_config == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (rx_ms == 0) {
    ESP_LOGE(TAG, "set_rx_duty_cycle: rx_ms must be > 0");
    return ESP_ERR_INVALID_ARG;
  }
  if (s_needs_reinit) {
    ESP_LOGE(TAG, "set_rx_duty_cycle: re-init required after cold sleep");
    return ESP_ERR_INVALID_STATE;
  }

  sx1262_state_t current = sx1262_fsm_get_state();
  if (current != SX1262_STATE_STDBY_RC && current != SX1262_STATE_STDBY_XOSC) {
    esp_err_t ret = sx1262_fsm_transition(SX1262_STATE_STDBY_RC);
    if (ret != ESP_OK) {
      return ret;
    }
    uint8_t stdby = SX1262_STDBY_RC;
    sx1262_cmd_write(s_hal, SX1262_OP_SET_STANDBY, &stdby, 1);
  }

  uint32_t rx_period = rx_ms * 64;
  uint32_t sleep_period = sleep_ms * 64;

  uint8_t params[6] = {
      (uint8_t)(rx_period >> 16),
      (uint8_t)(rx_period >> 8),
      (uint8_t)(rx_period),
      (uint8_t)(sleep_period >> 16),
      (uint8_t)(sleep_period >> 8),
      (uint8_t)(sleep_period),
  };

  uint8_t clr[2] = {(uint8_t)(SX1262_IRQ_ALL >> 8), (uint8_t)(SX1262_IRQ_ALL)};
  esp_err_t ret = sx1262_cmd_write(s_hal, SX1262_OP_CLEAR_IRQ_STATUS, clr, 2);
  if (ret != ESP_OK) {
    return ret;
  }

  s_hal->set_antenna(s_hal->ctx, SX1262_ANT_RX);

  ret = sx1262_fsm_transition(SX1262_STATE_RX);
  if (ret != ESP_OK) {
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    return ret;
  }

  ret = sx1262_cmd_write(s_hal, SX1262_OP_SET_RX_DUTY_CYCLE, params, 6);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SetRxDutyCycle failed");
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    sx1262_fsm_on_irq_complete();
    return ret;
  }

  ESP_LOGI(TAG,
           "RX duty cycle started: rx=%lu ms, sleep=%lu ms",
           (unsigned long)rx_ms,
           (unsigned long)sleep_ms);
  return ESP_OK;
}

static esp_err_t apply_workaround_w1(void) {
  uint8_t val = 0;
  esp_err_t ret = sx1262_cmd_read_register(s_hal, SX1262_REG_TX_MODULATION, &val, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "W1: read 0x%04X failed", SX1262_REG_TX_MODULATION);
    return ret;
  }

  if (s_config->bw == SX1262_LORA_BW_500) {
    val &= ~(1U << 2);
  } else {
    val |= (1U << 2);
  }

  ret = sx1262_cmd_write_register(s_hal, SX1262_REG_TX_MODULATION, &val, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "W1: write 0x%04X failed", SX1262_REG_TX_MODULATION);
    return ret;
  }

  ESP_LOGD(TAG,
           "W1 applied: reg 0x%04X = 0x%02X (bw=0x%02X)",
           SX1262_REG_TX_MODULATION,
           val,
           s_config->bw);
  return ESP_OK;
}

static esp_err_t update_packet_len(uint8_t len) {
  uint8_t header_type =
      s_config->is_implicit_hdr ? SX1262_LORA_HEADER_IMPLICIT : SX1262_LORA_HEADER_EXPLICIT;
  uint8_t crc_on = s_config->is_crc_on ? 0x01 : 0x00;
  uint8_t invert_iq = s_config->is_inverted_iq ? 0x01 : 0x00;

  uint8_t params[6] = {
      (uint8_t)(s_config->preamble_len >> 8),
      (uint8_t)(s_config->preamble_len & 0xFF),
      header_type,
      len,
      crc_on,
      invert_iq,
  };

  return sx1262_cmd_write(s_hal, SX1262_OP_SET_PACKET_PARAMS, params, 6);
}
