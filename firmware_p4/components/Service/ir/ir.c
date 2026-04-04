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

#include "ir.h"

#include <string.h>

#include <esp_log.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "IR";

// ── RX ──
static rmt_channel_handle_t s_rx_chan;
static QueueHandle_t s_rx_queue;
static rmt_symbol_word_t s_rx_buffer[128];
static rmt_receive_config_t s_rx_cfg = {
    .signal_range_min_ns = 1250,
    .signal_range_max_ns = 12000000,
};

// ── TX ──
static rmt_channel_handle_t s_tx_chan;
static rmt_encoder_handle_t s_tx_encoder;
static uint32_t s_current_carrier = 0;

// ── Init guards ──
static bool s_is_rx_inited = false;
static bool s_is_tx_inited = false;

// ── Last raw capture ──
static rmt_symbol_word_t s_last_raw[128];
static size_t s_last_raw_count = 0;

static bool rx_callback(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *data, void *ctx) {
    BaseType_t wake = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)ctx, data, &wake);
    return wake == pdTRUE;
}

void ir_rx_init(void) {
    if (s_is_rx_inited) return;
    rmt_rx_channel_config_t cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,
        .mem_block_symbols = 128,
        .gpio_num = IR_RX_GPIO,
        .flags.invert_in = false,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&cfg, &s_rx_chan));

    s_rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rx_callback };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_chan, &cbs, s_rx_queue));
    ESP_ERROR_CHECK(rmt_enable(s_rx_chan));
    ESP_ERROR_CHECK(rmt_receive(s_rx_chan, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_cfg));

    s_is_rx_inited = true;
    ESP_LOGI(TAG, "IR RX on GPIO %d", IR_RX_GPIO);
}

void ir_tx_init(void) {
    if (s_is_tx_inited) return;
    rmt_tx_channel_config_t cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_TX_GPIO,
        .mem_block_symbols = 128,
        .resolution_hz = 1000000,
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &s_tx_chan));

    rmt_copy_encoder_config_t enc_cfg = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&enc_cfg, &s_tx_encoder));
    ESP_ERROR_CHECK(rmt_enable(s_tx_chan));

    s_is_tx_inited = true;
    ESP_LOGI(TAG, "IR TX on GPIO %d", IR_TX_GPIO);
}

bool ir_receive(ir_data_t *out, uint32_t timeout_ms) {
    rmt_rx_done_event_data_t rx_data;
    if (xQueueReceive(s_rx_queue, &rx_data, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        return false;
    }

    // Store raw capture for later access
    s_last_raw_count = rx_data.num_symbols;
    if (s_last_raw_count > 128) s_last_raw_count = 128;
    memcpy(s_last_raw, rx_data.received_symbols, s_last_raw_count * sizeof(rmt_symbol_word_t));

    bool decoded = ir_decode(rx_data.received_symbols, rx_data.num_symbols, out);
    if (!decoded) {
        ir_print_raw(rx_data.received_symbols, rx_data.num_symbols);
    }

    ESP_ERROR_CHECK(rmt_receive(s_rx_chan, s_rx_buffer, sizeof(s_rx_buffer), &s_rx_cfg));
    return decoded;
}

static void ir_set_carrier(uint32_t freq_hz) {
    if (freq_hz == s_current_carrier) return;
    rmt_carrier_config_t carrier = {
        .duty_cycle = 0.33,
        .frequency_hz = freq_hz,
        .flags.polarity_active_low = false,
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(s_tx_chan, &carrier));
    s_current_carrier = freq_hz;
}

void ir_send(const ir_data_t *data) {
    rmt_symbol_word_t symbols[128];
    size_t count = ir_encode(data, symbols, 128);
    if (count == 0) return;

    ir_set_carrier(ir_carrier_freq(data->protocol));

    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(s_tx_chan, s_tx_encoder, symbols,
                                 count * sizeof(rmt_symbol_word_t), &tx_cfg));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_tx_chan, 1000));
}

void ir_send_raw(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier_hz) {
    ir_set_carrier(carrier_hz);
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(s_tx_chan, s_tx_encoder, symbols,
                                 count * sizeof(rmt_symbol_word_t), &tx_cfg));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_tx_chan, 1000));
}

const rmt_symbol_word_t *ir_last_raw(size_t *count) {
    if (count != NULL) *count = s_last_raw_count;
    return s_last_raw;
}

void ir_print_raw(rmt_symbol_word_t *symbols, size_t count) {
    ESP_LOGI(TAG, "--- Raw (%d symbols) ---", count);
    size_t max = (count > 40) ? 40 : count;
    for (size_t i = 0; i < max; i++) {
        ESP_LOGI(TAG, "[%2d] %5lu %5lu", i,
            (unsigned long)symbols[i].duration0,
            (unsigned long)symbols[i].duration1);
    }
    if (count > 40) ESP_LOGI(TAG, "... (%d omitted)", count - 40);
}

void ir_print_data(const ir_data_t *data) {
    ESP_LOGW(TAG, "Protocol: %-10s | Addr: 0x%04X | Cmd: 0x%04X%s",
        ir_protocol_name(data->protocol),
        data->address, data->command,
        data->repeat ? " [REPEAT]" : "");
}
