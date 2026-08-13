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

#include "ir.h"

#include <string.h>

#include "esp_log.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "IR";

static rmt_channel_handle_t s_rx_chan;
static QueueHandle_t s_rx_queue;
static rmt_symbol_word_t s_rx_buffer[IR_MAX_SYMBOLS];
static rmt_receive_config_t s_rx_cfg = {
    .signal_range_min_ns = IR_RX_MIN_NS,
    .signal_range_max_ns = IR_RX_MAX_NS,
};

static rmt_channel_handle_t s_tx_chan;
static rmt_encoder_handle_t s_tx_encoder;
static uint32_t s_current_carrier = 0;
static SemaphoreHandle_t s_mutex = NULL;

static bool s_is_rx_inited = false;
static bool s_is_tx_inited = false;
// True while the RX channel is armed for a frame. A one-shot rmt_receive() is
// consumed by every frame (including NEC repeat frames), so if a frame lands
// while no one is in ir_receive(), the channel goes idle — ir_rx_prime() re-arms
// it before the next capture. Set from the ISR, so volatile.
static volatile bool s_rx_armed = false;

static rmt_symbol_word_t s_last_raw[IR_MAX_SYMBOLS];
static size_t s_last_raw_count = 0;

static bool rx_callback(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *data, void *ctx);
static esp_err_t set_carrier(uint32_t freq_hz);
static esp_err_t transmit(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier_hz);

esp_err_t ir_rx_init(void) {
  if (s_is_rx_inited)
    return ESP_OK;

  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  // Non-DMA RX: DMA-backed rmt_receive() failed on this board and every error
  // path below used to `return` without deleting the channel, so the re-arm loop
  // leaked one RX channel per attempt until "no free rx channels". Now the config
  // is non-DMA and every failure jumps to `fail` to release the channel/queue.
  rmt_rx_channel_config_t cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = IR_RMT_RESOLUTION_HZ,
      .mem_block_symbols = IR_RX_MEM_BLOCK_SYMBOLS,
      .gpio_num = GPIO_IR_RX_PIN,
      .flags.invert_in = false,
      .flags.with_dma = false,
  };

  esp_err_t ret = rmt_new_rx_channel(&cfg, &s_rx_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_rx_channel failed: %s", esp_err_to_name(ret));
    s_rx_chan = NULL;
    return ret;
  }

  s_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
  if (s_rx_queue == NULL) {
    ret = ESP_ERR_NO_MEM;
    goto fail;
  }

  rmt_rx_event_callbacks_t cbs = {.on_recv_done = rx_callback};
  ret = rmt_rx_register_event_callbacks(s_rx_chan, &cbs, s_rx_queue);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_rx_register_event_callbacks failed: %s", esp_err_to_name(ret));
    goto fail;
  }

  ret = rmt_enable(s_rx_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable (rx) failed: %s", esp_err_to_name(ret));
    goto fail;
  }

  ret = rmt_receive(s_rx_chan, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(ret));
    rmt_disable(s_rx_chan);
    goto fail;
  }
  s_rx_armed = true;

  s_is_rx_inited = true;
  ESP_LOGI(TAG, "IR RX on GPIO %d", GPIO_IR_RX_PIN);
  return ESP_OK;

fail:
  if (s_rx_chan != NULL) {
    rmt_del_channel(s_rx_chan);
    s_rx_chan = NULL;
  }
  if (s_rx_queue != NULL) {
    vQueueDelete(s_rx_queue);
    s_rx_queue = NULL;
  }
  return ret;
}

void ir_rx_prime(void) {
  // Drop any stale/ambient frame, then re-arm the channel if a frame consumed the
  // one-shot receive while nobody was in ir_receive(). Flush first so a frame that
  // arrives after we re-arm isn't discarded.
  if (s_rx_queue != NULL)
    xQueueReset(s_rx_queue);
  if (s_rx_chan != NULL && !s_rx_armed) {
    if (rmt_receive(s_rx_chan, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_cfg) == ESP_OK)
      s_rx_armed = true;
  }
}

esp_err_t ir_tx_init(void) {
  if (s_is_tx_inited)
    return ESP_OK;

  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  rmt_tx_channel_config_t cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .gpio_num = GPIO_IR_TX_PIN,
      .mem_block_symbols = IR_TX_MEM_BLOCK_SYMBOLS,
      .resolution_hz = IR_RMT_RESOLUTION_HZ,
      .trans_queue_depth = IR_TX_QUEUE_DEPTH,
      .flags.invert_out = false,
      .flags.with_dma = false,
  };

  esp_err_t ret = rmt_new_tx_channel(&cfg, &s_tx_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(ret));
    s_tx_chan = NULL;
    return ret;
  }

  rmt_copy_encoder_config_t enc_cfg = {};
  ret = rmt_new_copy_encoder(&enc_cfg, &s_tx_encoder);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(ret));
    goto fail;
  }

  ret = rmt_enable(s_tx_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable (tx) failed: %s", esp_err_to_name(ret));
    goto fail;
  }

  s_is_tx_inited = true;
  ESP_LOGI(TAG, "IR TX on GPIO %d", GPIO_IR_TX_PIN);
  return ESP_OK;

fail:
  // Release the channel/encoder so a failed init doesn't leak a TX channel.
  if (s_tx_encoder != NULL) {
    rmt_del_encoder(s_tx_encoder);
    s_tx_encoder = NULL;
  }
  if (s_tx_chan != NULL) {
    rmt_del_channel(s_tx_chan);
    s_tx_chan = NULL;
  }
  return ret;
}

esp_err_t ir_receive(ir_data_t *out_data, uint32_t timeout_ms) {
  if (out_data == NULL)
    return ESP_ERR_INVALID_ARG;

  rmt_rx_done_event_data_t rx_data;
  if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
    return ESP_ERR_TIMEOUT;
  }

  if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
    s_last_raw_count = rx_data.num_symbols;
    if (s_last_raw_count > IR_MAX_SYMBOLS)
      s_last_raw_count = IR_MAX_SYMBOLS;
    memcpy(s_last_raw, rx_data.received_symbols, s_last_raw_count * sizeof(rmt_symbol_word_t));
    xSemaphoreGive(s_mutex);
  }

  bool is_decoded = ir_decode(rx_data.received_symbols, rx_data.num_symbols, out_data);
  if (!is_decoded) {
    ir_print_raw(rx_data.received_symbols, rx_data.num_symbols);
  }

  esp_err_t ret = rmt_receive(s_rx_chan, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_receive re-arm failed: %s", esp_err_to_name(ret));
    return ret;
  }
  s_rx_armed = true;

  return is_decoded ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t ir_send(const ir_data_t *data) {
  if (data == NULL)
    return ESP_ERR_INVALID_ARG;

  rmt_symbol_word_t symbols[IR_RMT_MEM_SYMBOLS];
  size_t count = ir_encode(data, symbols, IR_RMT_MEM_SYMBOLS);
  if (count == 0)
    return ESP_ERR_INVALID_ARG;
  return transmit(symbols, count, ir_carrier_freq(data->protocol));
}

esp_err_t ir_send_raw(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier_hz) {
  if (symbols == NULL || count == 0)
    return ESP_ERR_INVALID_ARG;
  return transmit(symbols, count, carrier_hz);
}

esp_err_t ir_get_last_raw(rmt_symbol_word_t *out_buf, size_t buf_max, size_t *out_count) {
  if (out_buf == NULL || buf_max == 0)
    return ESP_ERR_INVALID_ARG;

  if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
    if (out_count != NULL)
      *out_count = 0;
    return ESP_ERR_TIMEOUT;
  }

  size_t n = s_last_raw_count < buf_max ? s_last_raw_count : buf_max;
  memcpy(out_buf, s_last_raw, n * sizeof(rmt_symbol_word_t));
  if (out_count != NULL)
    *out_count = n;

  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

void ir_print_raw(const rmt_symbol_word_t *symbols, size_t count) {
  ESP_LOGD(TAG, "--- Raw (%d symbols) ---", count);
  size_t max = (count > IR_PRINT_MAX_SYMBOLS) ? IR_PRINT_MAX_SYMBOLS : count;
  for (size_t i = 0; i < max; i++) {
    ESP_LOGD(TAG,
             "[%2d] %5lu %5lu",
             i,
             (unsigned long)symbols[i].duration0,
             (unsigned long)symbols[i].duration1);
  }
  if (count > IR_PRINT_MAX_SYMBOLS)
    ESP_LOGD(TAG, "... (%d omitted)", count - IR_PRINT_MAX_SYMBOLS);
}

void ir_print_data(const ir_data_t *data) {
  ESP_LOGI(TAG,
           "Protocol: %-10s | Addr: 0x%08lX | Cmd: 0x%08lX%s",
           ir_protocol_name(data->protocol),
           (unsigned long)data->address,
           (unsigned long)data->command,
           data->repeat ? " [REPEAT]" : "");
}

static bool rx_callback(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *data, void *ctx) {
  (void)ch;
  s_rx_armed = false; // this frame consumed the one-shot receive
  BaseType_t wake = pdFALSE;
  xQueueSendFromISR((QueueHandle_t)ctx, data, &wake);
  return wake == pdTRUE;
}

static esp_err_t set_carrier(uint32_t freq_hz) {
  if (freq_hz == s_current_carrier)
    return ESP_OK;
  rmt_carrier_config_t carrier = {
      .duty_cycle = IR_CARRIER_DUTY_CYCLE,
      .frequency_hz = freq_hz,
      .flags.polarity_active_low = false,
  };
  esp_err_t ret = rmt_apply_carrier(s_tx_chan, &carrier);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "rmt_apply_carrier failed: %s", esp_err_to_name(ret));
    return ret;
  }
  s_current_carrier = freq_hz;
  return ESP_OK;
}

static esp_err_t transmit(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier_hz) {
  if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t ret = set_carrier(carrier_hz);
  if (ret != ESP_OK) {
    xSemaphoreGive(s_mutex);
    return ret;
  }

  rmt_transmit_config_t tx_cfg = {.loop_count = 0};
  ret = rmt_transmit(s_tx_chan, s_tx_encoder, symbols, count * sizeof(rmt_symbol_word_t), &tx_cfg);
  if (ret == ESP_OK) {
    ret = rmt_tx_wait_all_done(s_tx_chan, IR_TX_WAIT_MS);
  }

  xSemaphoreGive(s_mutex);
  return ret;
}