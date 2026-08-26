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

#include "host_link_ir.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host_link.h"
#include "ir.h"
#include "spi_protocol.h"
#include "sys_prio.h"

static const char *TAG = "HOST_LINK_IR";

#define IR_TX_MIN_PAYLOAD   10  // [proto u8][addr u32][cmd u32][repeat u8]
#define IR_RAW_HDR_LEN      6   // [carrier u32][count u16]
#define IR_RAW_MAX_SYMBOLS  512 // cap per IR_TX_RAW
#define IR_FRAME_LEN        10  // decoded-signal wire layout (same as IR_TX)
#define IR_RX_RECV_MS       200 // poll window: bounds stop-flag latency

static TaskHandle_t s_rx_task = NULL;
static volatile bool s_rx_run = false;
static rmt_symbol_word_t s_raw[IR_RAW_MAX_SYMBOLS];

// Pack an ir_data_t into the 10-byte wire layout shared by IR_TX and RX_FRAME.
static void encode_signal(const ir_data_t *d, uint8_t out[IR_FRAME_LEN]) {
  out[0] = (uint8_t)d->protocol;
  memcpy(out + 1, &d->address, sizeof(uint32_t));
  memcpy(out + 5, &d->command, sizeof(uint32_t));
  out[9] = d->repeat ? 1 : 0;
}

static void ir_rx_task(void *arg) {
  (void)arg;
  ir_data_t data;
  while (s_rx_run) {
    ir_rx_prime();
    if (ir_receive(&data, IR_RX_RECV_MS) == ESP_OK) {
      uint8_t frame[IR_FRAME_LEN];
      encode_signal(&data, frame);
      host_link_emit_stream(SPI_CAT_IR, SPI_ID_IR_RX_FRAME & 0xFF, frame, sizeof(frame));
    }
  }
  ir_rx_deinit();
  s_rx_task = NULL;
  vTaskDelete(NULL);
}

void host_ir_stop_rx(void) {
  if (s_rx_task == NULL)
    return;
  s_rx_run = false;
  // The task clears s_rx_task and self-deletes once its poll window elapses.
  for (int i = 0; i < 20 && s_rx_task != NULL; i++)
    vTaskDelay(pdMS_TO_TICKS(20));
}

uint8_t host_ir_handle(uint16_t cmd,
                       const uint8_t *payload,
                       uint16_t plen,
                       uint8_t *out,
                       uint16_t out_cap,
                       uint16_t *out_len) {
  (void)out;
  (void)out_cap;
  *out_len = 0;

  switch (cmd) {
    case SPI_ID_IR_TX: {
      if (plen < IR_TX_MIN_PAYLOAD)
        return SPI_STATUS_INVALID_ARG;
      ir_data_t d = {0};
      d.protocol = (ir_protocol_t)payload[0];
      memcpy(&d.address, payload + 1, sizeof(uint32_t));
      memcpy(&d.command, payload + 5, sizeof(uint32_t));
      d.repeat = payload[9] != 0;
      if (ir_tx_init() != ESP_OK)
        return SPI_STATUS_ERROR;
      esp_err_t r = ir_send(&d);
      ir_tx_deinit();
      return (r == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_IR_TX_RAW: {
      if (plen < IR_RAW_HDR_LEN)
        return SPI_STATUS_INVALID_ARG;
      uint32_t carrier_hz = 0;
      uint16_t count = 0;
      memcpy(&carrier_hz, payload, sizeof(uint32_t));
      memcpy(&count, payload + 4, sizeof(uint16_t));
      if (count == 0 || count > IR_RAW_MAX_SYMBOLS)
        return SPI_STATUS_INVALID_ARG;
      if (plen < (uint16_t)(IR_RAW_HDR_LEN + count * sizeof(rmt_symbol_word_t)))
        return SPI_STATUS_INVALID_ARG;
      memcpy(s_raw, payload + IR_RAW_HDR_LEN, count * sizeof(rmt_symbol_word_t));
      if (ir_tx_init() != ESP_OK)
        return SPI_STATUS_ERROR;
      esp_err_t r = ir_send_raw(s_raw, count, carrier_hz);
      ir_tx_deinit();
      return (r == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_IR_RX_START: {
      if (s_rx_task != NULL)
        return SPI_STATUS_BUSY;
      if (ir_rx_init() != ESP_OK)
        return SPI_STATUS_ERROR;
      s_rx_run = true;
      if (xTaskCreatePinnedToCore(
              ir_rx_task, "ir_rx", 4096, NULL, SYS_PRIO_SERVICE_LO, &s_rx_task, SYS_CORE_RADIO) !=
          pdPASS) {
        s_rx_run = false;
        ir_rx_deinit();
        return SPI_STATUS_ERROR;
      }
      return SPI_STATUS_OK;
    }

    case SPI_ID_IR_RX_STOP:
      host_ir_stop_rx();
      return SPI_STATUS_OK;

    case SPI_ID_IR_TORCH_ON:
      return (ir_flash_on() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    case SPI_ID_IR_TORCH_OFF:
      return (ir_flash_off() == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;

    default:
      ESP_LOGW(TAG, "Unknown IR op 0x%04X", cmd);
      return SPI_STATUS_UNSUPPORTED;
  }
}
