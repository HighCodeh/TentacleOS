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

#include "subghz_transmitter.h"
#include "cc1101.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "pin_def.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "SUBGHZ_TX";

#define RMT_TX_GPIO       8  // GDO0 (GPIO_SDA_PIN)
#define RMT_RESOLUTION_HZ 1000000 // 1MHz -> 1us per tick

#define TX_QUEUE_SIZE     10

typedef struct {
  int32_t *timings;
  size_t count;
} tx_item_t;

static rmt_channel_handle_t tx_channel = NULL;
static rmt_encoder_handle_t copy_encoder = NULL;
static QueueHandle_t tx_queue = NULL;
static TaskHandle_t tx_task_handle = NULL;
static volatile bool s_is_running = false;

static void subghz_tx_task(void *pvParameters) {
  tx_item_t item;

  ESP_LOGI(TAG, "TX Task Started");

  while (s_is_running) {
    if (xQueueReceive(tx_queue, &item, pdMS_TO_TICKS(200)) == pdPASS) {

      ESP_LOGI(TAG, "Processing TX item: %d symbols", item.count);

      // 1. Convert Timings to RMT Symbols
      // Each rmt_symbol_word_t holds 2 pulses in the packed format for Copy Encoder
      size_t num_words = (item.count + 1) / 2; 
      rmt_symbol_word_t *symbols = heap_caps_malloc(num_words * sizeof(rmt_symbol_word_t), MALLOC_CAP_DEFAULT);

      if (symbols) {
        memset(symbols, 0, num_words * sizeof(rmt_symbol_word_t));

        for (size_t i = 0; i < item.count; i++) {
          size_t word_idx = i / 2;
          int pulse_idx = i % 2;

          int32_t t = item.timings[i];
          uint32_t duration = (t > 0) ? t : -t;
          // Logic: Positive=Active(1), Negative=Gap(0)
          // Note: RMT 'invert_out' handles the electrical inversion if needed.
          uint32_t level = (t > 0) ? 1 : 0;

          if (duration > 32767) duration = 32767;

          if (pulse_idx == 0) {
            symbols[word_idx].duration0 = duration;
            symbols[word_idx].level0 = level;
          } else {
            symbols[word_idx].duration1 = duration;
            symbols[word_idx].level1 = level;
          }
        }

        // 2. Enable CC1101 TX
        cc1101_enter_tx_mode();

        // 3. Transmit
        rmt_transmit_config_t tx_config = {
          .loop_count = 0,
        };

        ESP_ERROR_CHECK(rmt_transmit(tx_channel, copy_encoder, symbols, num_words * sizeof(rmt_symbol_word_t), &tx_config));

        // 4. Wait for completion
        // We use a timeout instead of infinite wait to avoid hanging the task forever
        rmt_tx_wait_all_done(tx_channel, 2000); // 2s max timeout

        // 5. Cleanup Symbols
        free(symbols);

        // Back to Idle
        cc1101_strobe(CC1101_SIDLE);
      } else {
        ESP_LOGE(TAG, "Failed to allocate symbol buffer");
      }

      // Free the timing buffer allocated in send_raw
      if (item.timings) {
        free(item.timings);
      }

      ESP_LOGI(TAG, "TX Complete");
    }
  }

  vTaskDelete(NULL);
}

void subghz_tx_init(void) {
  if (s_is_running) return;

  ESP_LOGI(TAG, "Initializing SubGhz Transmitter (Async Task)");

  // SAFETY: Ensure CC1101 is NOT driving the pin (RX mode) before we enable RMT TX
  cc1101_strobe(CC1101_SIDLE);

      // 1. Create RMT TX Channel
      rmt_tx_channel_config_t tx_channel_cfg = {
          .clk_src = RMT_CLK_SRC_DEFAULT,
          .resolution_hz = RMT_RESOLUTION_HZ,
          .mem_block_symbols = 64,
          .trans_queue_depth = 4,
          .gpio_num = RMT_TX_GPIO,
          .flags.invert_out = false, // Disable inversion
      };  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &tx_channel));

  // 2. Create Copy Encoder
  rmt_copy_encoder_config_t copy_encoder_cfg = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_cfg, &copy_encoder));

  // 3. Enable RMT Channel
  ESP_ERROR_CHECK(rmt_enable(tx_channel));

  // 4. Configure CC1101
  cc1101_enable_async_mode(433920000);
  cc1101_strobe(CC1101_SIDLE);

  // 5. Create Queue and Task
  tx_queue = xQueueCreate(TX_QUEUE_SIZE, sizeof(tx_item_t));
  if (tx_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create TX Queue");
    return;
  }

  s_is_running = true;
  xTaskCreatePinnedToCore(subghz_tx_task, "subghz_tx", 4096, NULL, 5, &tx_task_handle, 1);
}

void subghz_tx_stop(void) {
  if (!s_is_running) return;

  ESP_LOGI(TAG, "Stopping SubGhz Transmitter");
  s_is_running = false;

  // Wait slightly for task to finish current loop
  vTaskDelay(pdMS_TO_TICKS(100));

  // Drain Queue
  if (tx_queue) {
    tx_item_t item;
    while (xQueueReceive(tx_queue, &item, 0) == pdPASS) {
      if (item.timings) free(item.timings);
    }
    vQueueDelete(tx_queue);
    tx_queue = NULL;
  }

  // Cleanup RMT
  if (tx_channel) {
    rmt_disable(tx_channel);
    rmt_del_channel(tx_channel);
    tx_channel = NULL;
  }
  if (copy_encoder) {
    rmt_del_encoder(copy_encoder);
    copy_encoder = NULL;
  }

  // Put radio in Idle
  cc1101_strobe(CC1101_SIDLE);

  tx_task_handle = NULL;
}

void subghz_tx_send_raw(const int32_t *timings, size_t count) {
  if (!s_is_running || !timings || count == 0) return;

  // Allocate memory for this specific transmission
  // The task will free this memory after processing
  int32_t *timings_copy = heap_caps_malloc(count * sizeof(int32_t), MALLOC_CAP_DEFAULT);
  if (!timings_copy) {
    ESP_LOGE(TAG, "OOM: Failed to copy TX timings");
    return;
  }

  memcpy(timings_copy, timings, count * sizeof(int32_t));

  tx_item_t item;
  item.timings = timings_copy;
  item.count = count;

  if (xQueueSend(tx_queue, &item, pdMS_TO_TICKS(100)) != pdPASS) {
    ESP_LOGE(TAG, "TX Queue Full - Dropping packet");
    free(timings_copy);
  }
}
