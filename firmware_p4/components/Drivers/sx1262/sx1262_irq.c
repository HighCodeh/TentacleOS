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

#include "sx1262_irq.h"

#include <string.h>

#include "esp_log.h"

#include "sx1262_regs.h"
#include "sx1262_cmd.h"
#include "sx1262_fsm.h"

static const char *TAG = "SX1262_IRQ";

static sx1262_hal_t *s_hal = NULL;
static const sx1262_config_t *s_config = NULL;
static sx1262_callbacks_t s_cbs = {0};

static sx1262_packet_t s_rx_ring[SX1262_RX_RING_SIZE];
static volatile uint8_t s_rx_head = 0;
static volatile uint8_t s_rx_tail = 0;

static esp_err_t read_rx_packet(sx1262_packet_t *out_pkt, uint16_t irq_flags);
static void apply_workaround_w3(void);

void sx1262_irq_init(sx1262_hal_t *hal,
                     const sx1262_config_t *config,
                     const sx1262_callbacks_t *cbs) {
  s_hal = hal;
  s_config = config;
  s_rx_head = 0;
  s_rx_tail = 0;

  if (cbs != NULL) {
    memcpy(&s_cbs, cbs, sizeof(sx1262_callbacks_t));
  } else {
    memset(&s_cbs, 0, sizeof(sx1262_callbacks_t));
  }

  ESP_LOGI(TAG, "IRQ subsystem initialized, ring buffer reset");
}

esp_err_t sx1262_irq_process(void) {
  if (s_hal == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t irq_buf[2] = {0};
  esp_err_t ret = sx1262_cmd_read(s_hal, SX1262_OP_GET_IRQ_STATUS, irq_buf, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GetIrqStatus failed");
    return ret;
  }
  uint16_t irq_flags = ((uint16_t)irq_buf[0] << 8) | irq_buf[1];

  if (irq_flags == 0) {
    return ESP_OK;
  }

  uint8_t clr_params[2] = {(uint8_t)(irq_flags >> 8), (uint8_t)(irq_flags)};
  ret = sx1262_cmd_write(s_hal, SX1262_OP_CLEAR_IRQ_STATUS, clr_params, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ClearIrqStatus failed");
    return ret;
  }

  if (irq_flags & SX1262_IRQ_TX_DONE) {
    ESP_LOGD(TAG, "IRQ: TxDone");
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    sx1262_fsm_on_irq_complete();

    if (s_cbs.on_tx_done != NULL) {
      s_cbs.on_tx_done(s_cbs.cb_ctx);
    }
  }

  if (irq_flags & SX1262_IRQ_RX_DONE) {
    ESP_LOGD(TAG, "IRQ: RxDone");
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);

    if (s_config != NULL && s_config->is_implicit_hdr) {
      apply_workaround_w3();
    }

    sx1262_packet_t pkt = {0};
    ret = read_rx_packet(&pkt, irq_flags);
    if (ret == ESP_OK) {
      s_hal->enter_critical(s_hal->ctx);
      uint8_t next_head = (s_rx_head + 1) % SX1262_RX_RING_SIZE;
      if (next_head == s_rx_tail) {
        s_hal->exit_critical(s_hal->ctx);
        ESP_LOGW(TAG, "Ring buffer full — packet discarded");
      } else {
        memcpy(&s_rx_ring[s_rx_head], &pkt, sizeof(sx1262_packet_t));
        s_rx_head = next_head;
        s_hal->exit_critical(s_hal->ctx);
      }

      if (s_cbs.on_rx_done != NULL) {
        s_cbs.on_rx_done(&pkt, s_cbs.cb_ctx);
      }
    }

    sx1262_fsm_on_irq_complete();
  }

  if (irq_flags & SX1262_IRQ_TIMEOUT) {
    ESP_LOGD(TAG, "IRQ: Timeout");
    s_hal->set_antenna(s_hal->ctx, SX1262_ANT_OFF);
    sx1262_fsm_on_irq_complete();

    if (s_cbs.on_timeout != NULL) {
      s_cbs.on_timeout(s_cbs.cb_ctx);
    }
  }

  if ((irq_flags & SX1262_IRQ_CRC_ERR) && !(irq_flags & SX1262_IRQ_RX_DONE)) {
    ESP_LOGW(TAG, "IRQ: CRC error (standalone)");
    if (s_cbs.on_error != NULL) {
      s_cbs.on_error(ESP_ERR_INVALID_CRC, s_cbs.cb_ctx);
    }
  }
  if ((irq_flags & SX1262_IRQ_HEADER_ERR) && !(irq_flags & SX1262_IRQ_RX_DONE)) {
    ESP_LOGW(TAG, "IRQ: Header error (standalone)");
    if (s_cbs.on_error != NULL) {
      s_cbs.on_error(ESP_FAIL, s_cbs.cb_ctx);
    }
  }

  if (irq_flags & SX1262_IRQ_CAD_DONE) {
    bool is_detected = (irq_flags & SX1262_IRQ_CAD_DETECTED) != 0;
    ESP_LOGD(TAG, "IRQ: CAD done, detected=%d", is_detected);
    sx1262_fsm_on_irq_complete();

    if (s_cbs.on_cad_done != NULL) {
      s_cbs.on_cad_done(is_detected, s_cbs.cb_ctx);
    }
  }

  return ESP_OK;
}

esp_err_t sx1262_irq_get_packet(sx1262_packet_t *out_packet) {
  if (out_packet == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_hal == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  s_hal->enter_critical(s_hal->ctx);

  if (s_rx_head == s_rx_tail) {
    s_hal->exit_critical(s_hal->ctx);
    return ESP_ERR_NOT_FOUND;
  }

  memcpy(out_packet, &s_rx_ring[s_rx_tail], sizeof(sx1262_packet_t));
  s_rx_tail = (s_rx_tail + 1) % SX1262_RX_RING_SIZE;

  s_hal->exit_critical(s_hal->ctx);
  return ESP_OK;
}

bool sx1262_irq_has_packet(void) {
  if (s_hal == NULL) {
    return false;
  }
  s_hal->enter_critical(s_hal->ctx);
  bool has = (s_rx_head != s_rx_tail);
  s_hal->exit_critical(s_hal->ctx);
  return has;
}

static esp_err_t read_rx_packet(sx1262_packet_t *out_pkt, uint16_t irq_flags) {
  uint8_t rx_buf_status[2] = {0};
  esp_err_t ret = sx1262_cmd_read(s_hal, SX1262_OP_GET_RX_BUFFER_STATUS, rx_buf_status, 2);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GetRxBufferStatus failed");
    return ret;
  }

  uint8_t payload_len = rx_buf_status[0];
  uint8_t buf_offset = rx_buf_status[1];

  uint8_t pkt_status[3] = {0};
  ret = sx1262_cmd_read(s_hal, SX1262_OP_GET_PACKET_STATUS, pkt_status, 3);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "GetPacketStatus failed");
    return ret;
  }

  if (payload_len > 0) {
    ret = sx1262_cmd_read_buffer(s_hal, buf_offset, out_pkt->buf, payload_len);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "ReadBuffer failed");
      return ret;
    }
  }

  out_pkt->len = payload_len;
  out_pkt->rssi_pkt_dbm = -(int16_t)(pkt_status[0] / 2);
  out_pkt->snr_pkt_db = (int8_t)pkt_status[1] / 4;
  out_pkt->signal_rssi_dbm = -(int16_t)(pkt_status[2] / 2);
  out_pkt->has_crc_error = (irq_flags & SX1262_IRQ_CRC_ERR) != 0;
  out_pkt->has_header_error = (irq_flags & SX1262_IRQ_HEADER_ERR) != 0;

  return ESP_OK;
}

static void apply_workaround_w3(void) {
  uint8_t val = 0;
  esp_err_t ret = sx1262_cmd_read_register(s_hal, SX1262_REG_RTC_CTRL, &val, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "W3: read 0x%04X failed", SX1262_REG_RTC_CTRL);
    return;
  }
  val &= ~(1U << 0);
  sx1262_cmd_write_register(s_hal, SX1262_REG_RTC_CTRL, &val, 1);

  uint8_t clr = 0x00;
  ret = sx1262_cmd_read_register(s_hal, SX1262_REG_EVT_CLR, &clr, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "W3: read 0x%04X failed", SX1262_REG_EVT_CLR);
    return;
  }
  clr |= (1U << 1);
  sx1262_cmd_write_register(s_hal, SX1262_REG_EVT_CLR, &clr, 1);

  ESP_LOGD(TAG, "W3 applied: RTC stopped + event cleared");
}
