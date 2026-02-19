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

#include "subghz_receiver.h"
#include "cc1101.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "pin_def.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "subghz_protocol_registry.h"

static const char *TAG = "SUBGHZ_RX";

#define RMT_RX_GPIO       8  // GDO0 (GPIO_SDA_PIN)
#define RMT_RESOLUTION_HZ 1000000 // 1MHz -> 1us per tick

// Configuration
#define RX_BUFFER_SIZE    1024 // Number of symbols
#define MIN_PULSE_NS      1000 // Hardware Filter limit (~3us)
#define SOFTWARE_FILTER_US 15  // Software filter (15us) to clean up noise
#define MAX_PULSE_NS      10000000 // 10ms idle timeout

// Hopping Frequencies
static const uint32_t HOOP_FREQS[] = {
  433920000,
  868350000,
  315000000,
  300000000,
  390000000,
  418000000,
  915000000,
  302750000,
  303870000,
  304250000,
  310000000,
  318000000
};
static const size_t HOOP_FREQS_COUNT = sizeof(HOOP_FREQS) / sizeof(HOOP_FREQS[0]);

static TaskHandle_t rx_task_handle = NULL;
static volatile bool s_is_running = false;
static subghz_mode_t s_rx_mode = SUBGHZ_MODE_SCAN;
static subghz_modulation_t s_rx_mod = SUBGHZ_MODULATION_ASK;
static uint32_t s_rx_freq = 433920000;
static bool s_hopping_active = false;
static int s_hoop_idx = 0;

static rmt_channel_handle_t rx_channel = NULL;
static QueueHandle_t rx_queue = NULL;

static bool subghz_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
  BaseType_t high_task_wakeup = pdFALSE;
  QueueHandle_t queue = (QueueHandle_t)user_data;
  xQueueSendFromISR(queue, edata, &high_task_wakeup);
  return high_task_wakeup == pdTRUE;
}

static void subghz_rx_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting RMT RX Task - Mode: %s, Mod: %s, Freq: %lu", 
           s_rx_mode == SUBGHZ_MODE_RAW ? "RAW" : "SCAN",
           s_rx_mod == SUBGHZ_MODULATION_FSK ? "FSK" : "ASK",
           s_rx_freq);

  // 1. Configure RMT RX Channel
  rmt_rx_channel_config_t rx_channel_cfg = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = RMT_RESOLUTION_HZ,
    .mem_block_symbols = 256, 
    .gpio_num = RMT_RX_GPIO,
    .flags.invert_in = false,
    .flags.with_dma = true,
  };

  ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &rx_channel));

  rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
  rmt_rx_event_callbacks_t cbs = {
    .on_recv_done = subghz_rx_done_callback,
  };
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_channel, &cbs, rx_queue));

  ESP_ERROR_CHECK(rmt_enable(rx_channel));

  if (s_rx_mod == SUBGHZ_MODULATION_FSK) {
    cc1101_enable_fsk_mode(s_rx_freq);
  } else {
    cc1101_enable_async_mode(s_rx_freq); // ASK
  }

  // 5. Start Receiving Loop
  s_is_running = true;
  ESP_LOGI(TAG, "Waiting for signals...");

  rmt_receive_config_t receive_config = {
    .signal_range_min_ns = MIN_PULSE_NS, // Hardware Filter
    .signal_range_max_ns = MAX_PULSE_NS, // Idle Timeout
  };

  rmt_rx_done_event_data_t rx_data;
  rmt_symbol_word_t *raw_symbols = NULL;
  int32_t *decode_buffer = NULL;

  size_t raw_symbols_size = sizeof(rmt_symbol_word_t) * RX_BUFFER_SIZE;
  raw_symbols = heap_caps_aligned_alloc(64, raw_symbols_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!raw_symbols) {
    ESP_LOGE(TAG, "Failed to allocate RMT RX buffer");
    goto cleanup;
  }

  decode_buffer = malloc(RX_BUFFER_SIZE * 2 * sizeof(int32_t));
  if (!decode_buffer) {
    ESP_LOGE(TAG, "Failed to allocate decode buffer");
    goto cleanup;
  }

  ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, raw_symbols_size, &receive_config));

  TickType_t last_hop_time = xTaskGetTickCount();
  const TickType_t hop_interval = pdMS_TO_TICKS(5000);

  while (s_is_running) {
    // Hopping Logic
    if (s_hopping_active && (xTaskGetTickCount() - last_hop_time > hop_interval)) {
      s_hoop_idx = (s_hoop_idx + 1) % HOOP_FREQS_COUNT;
      s_rx_freq = HOOP_FREQS[s_hoop_idx];

      ESP_LOGI(TAG, "Hopping to: %lu Hz", (long unsigned int)s_rx_freq);

      cc1101_strobe(CC1101_SIDLE);
      cc1101_set_frequency(s_rx_freq);
      cc1101_strobe(CC1101_SRX);

      last_hop_time = xTaskGetTickCount();
    }

    if (xQueueReceive(rx_queue, &rx_data, pdMS_TO_TICKS(100)) == pdPASS) {

      if (rx_data.num_symbols > 0) {
        size_t decode_idx = 0;

        // Process pulses into linear buffer
        for (size_t i = 0; i < rx_data.num_symbols; i++) {
          rmt_symbol_word_t *sym = &rx_data.received_symbols[i];

          // Pulse 0 + Software Filter
          if (sym->duration0 >= SOFTWARE_FILTER_US) {
            int32_t val = sym->level0 ? (int)sym->duration0 : -(int)sym->duration0;
            if (decode_buffer && decode_idx < RX_BUFFER_SIZE * 2) {
              decode_buffer[decode_idx++] = val;
            }
          }

          // Pulse 1 + Software Filter
          if (sym->duration1 >= SOFTWARE_FILTER_US) {
            int32_t val = sym->level1 ? (int)sym->duration1 : -(int)sym->duration1;
            if (decode_buffer && decode_idx < RX_BUFFER_SIZE * 2) {
              decode_buffer[decode_idx++] = val;
            }
          }
        }

        if (decode_buffer && decode_idx > 0) {
          if (s_rx_mode == SUBGHZ_MODE_RAW) {
            // RAW MODE: Always print
            printf("RAW: ");
            for (size_t k = 0; k < decode_idx; k++) {
              printf("%ld ", (long)decode_buffer[k]);
            }
            printf("\n");
          } else {
            // SCAN MODE: Try to decode
            subghz_data_t decoded = {0};
            if (subghz_process_raw(decode_buffer, decode_idx, &decoded)) {
              ESP_LOGI(TAG, "DECODED: Protocol: %s | Serial: 0x%lX | Btn: 0x%X | Bits: %d | Freq: %lu", 
                       decoded.protocol_name, decoded.serial, decoded.btn, decoded.bit_count, (long unsigned int)s_rx_freq);
            } else {
              // DEBUG: Show RAW even if failed, to help calibrate decoders
              ESP_LOGW(TAG, "Unknown Protocol detected (Length: %d) at %lu Hz. RAW Data below:", (int)decode_idx, (long unsigned int)s_rx_freq);
              printf("RAW: ");
              for (size_t k = 0; k < decode_idx; k++) {
                printf("%ld ", (long)decode_buffer[k]);
              }
              printf("\n");
            }
          }
        }
      }

      // Continue receiving
      if (s_is_running) {
        ESP_ERROR_CHECK(rmt_receive(rx_channel, raw_symbols, raw_symbols_size, &receive_config));
      }
    }
  }

cleanup:
  // Cleanup
  if (raw_symbols) free(raw_symbols);
  if (decode_buffer) free(decode_buffer);
  cc1101_strobe(CC1101_SIDLE); // Stop Radio (High-Z)
  rmt_disable(rx_channel);
  rmt_del_channel(rx_channel);
  vQueueDelete(rx_queue);
  rx_channel = NULL;
  rx_queue = NULL;
  vTaskDelete(NULL);
}

void subghz_receiver_start(subghz_mode_t mode, subghz_modulation_t mod, uint32_t freq) {
  if (s_is_running) return;
  s_rx_mode = mode;
  s_rx_mod = mod;

  if (freq == 0) {
    s_hopping_active = true;
    s_hoop_idx = 0;
    s_rx_freq = HOOP_FREQS[0];
  } else {
    s_hopping_active = false;
    s_rx_freq = freq;
  }

  xTaskCreatePinnedToCore(subghz_rx_task, "subghz_rx", 4096, NULL, 5, &rx_task_handle, 1);
}

void subghz_receiver_stop(void) {
  s_is_running = false;
}

bool subghz_receiver_is_running(void) {
  return s_is_running;
}
