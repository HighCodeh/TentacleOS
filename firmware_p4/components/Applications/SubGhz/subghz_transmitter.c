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

#include <string.h>

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "cc1101.h"
#include "pin_def.h"

static const char *TAG = "SUBGHZ_TX";

#define RMT_TX_GPIO            GPIO_SCL_PIN
#define RMT_RESOLUTION_HZ      1000000
#define TX_QUEUE_SIZE           10
#define TX_QUEUE_RECV_MS        200
#define TX_QUEUE_SEND_MS        100
#define TX_WAIT_TIMEOUT_MS      2000
#define TX_TASK_STACK_SIZE      4096
#define TX_TASK_PRIORITY        5
#define TX_TASK_CORE            1
#define TX_STOP_DELAY_MS        100
#define RMT_MEM_BLOCK_SYMBOLS   64
#define RMT_TRANS_QUEUE_DEPTH   4
#define RMT_MAX_DURATION        32767
#define TX_DEFAULT_FREQ         433920000

typedef struct {
  int32_t *timings;
  size_t count;
} tx_item_t;

static rmt_channel_handle_t s_tx_channel = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;
static QueueHandle_t s_tx_queue = NULL;
static TaskHandle_t s_tx_task_handle = NULL;
static volatile bool s_is_running = false;

static void subghz_tx_task(void *pvParameters) {
  tx_item_t item;

  ESP_LOGI(TAG, "TX Task Started");

  while (s_is_running) {
    if (xQueueReceive(s_tx_queue, &item, pdMS_TO_TICKS(TX_QUEUE_RECV_MS)) != pdPASS) {
      continue;
    }

    ESP_LOGI(TAG, "Processing TX item: %d symbols", (int)item.count);

    size_t num_words = (item.count + 1) / 2;
    rmt_symbol_word_t *symbols = heap_caps_malloc(num_words * sizeof(rmt_symbol_word_t),
                                                  MALLOC_CAP_DEFAULT);

    if (symbols == NULL) {
      ESP_LOGE(TAG, "Failed to allocate symbol buffer");
      if (item.timings != NULL) {
        free(item.timings);
      }
      ESP_LOGI(TAG, "TX Complete");
      continue;
    }

    memset(symbols, 0, num_words * sizeof(rmt_symbol_word_t));

    for (size_t i = 0; i < item.count; i++) {
      size_t word_idx = i / 2;
      int pulse_idx = i % 2;

      int32_t t = item.timings[i];
      uint32_t duration = (t > 0) ? (uint32_t)t : (uint32_t)(-t);
      uint32_t level = (t > 0) ? 1 : 0;

      if (duration > RMT_MAX_DURATION) {
        duration = RMT_MAX_DURATION;
      }

      if (pulse_idx == 0) {
        symbols[word_idx].duration0 = duration;
        symbols[word_idx].level0 = level;
      } else {
        symbols[word_idx].duration1 = duration;
        symbols[word_idx].level1 = level;
      }
    }

    cc1101_enter_tx_mode();

    rmt_transmit_config_t tx_config = {
      .loop_count = 0,
    };

    ESP_ERROR_CHECK(rmt_transmit(s_tx_channel, s_copy_encoder, symbols,
                                 num_words * sizeof(rmt_symbol_word_t), &tx_config));

    esp_err_t wait_err = rmt_tx_wait_all_done(s_tx_channel, TX_WAIT_TIMEOUT_MS);
    if (wait_err != ESP_OK) {
      ESP_LOGW(TAG, "TX wait timed out, skipping buffer free");
    } else {
      free(symbols);
    }

    cc1101_strobe(CC1101_SIDLE);

    if (item.timings != NULL) {
      free(item.timings);
    }

    ESP_LOGI(TAG, "TX Complete");
  }

  vTaskDelete(NULL);
}

esp_err_t subghz_tx_init(void) {
  if (s_is_running) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing SubGhz Transmitter (Async Task)");

  cc1101_strobe(CC1101_SIDLE);

  rmt_tx_channel_config_t tx_channel_cfg = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = RMT_RESOLUTION_HZ,
    .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
    .trans_queue_depth = RMT_TRANS_QUEUE_DEPTH,
    .gpio_num = RMT_TX_GPIO,
    .flags.invert_out = false,
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_channel_cfg, &s_tx_channel));

  rmt_copy_encoder_config_t copy_encoder_cfg = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_cfg, &s_copy_encoder));

  ESP_ERROR_CHECK(rmt_enable(s_tx_channel));

  cc1101_enable_async_mode(TX_DEFAULT_FREQ);
  cc1101_strobe(CC1101_SIDLE);

  s_tx_queue = xQueueCreate(TX_QUEUE_SIZE, sizeof(tx_item_t));
  if (s_tx_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create TX Queue");
    return ESP_ERR_NO_MEM;
  }

  s_is_running = true;
  xTaskCreatePinnedToCore(subghz_tx_task, "subghz_tx", TX_TASK_STACK_SIZE,
                          NULL, TX_TASK_PRIORITY, &s_tx_task_handle, TX_TASK_CORE);
  return ESP_OK;
}

void subghz_tx_stop(void) {
  if (!s_is_running) {
    return;
  }

  ESP_LOGI(TAG, "Stopping SubGhz Transmitter");
  s_is_running = false;

  vTaskDelay(pdMS_TO_TICKS(TX_STOP_DELAY_MS));

  if (s_tx_queue != NULL) {
    tx_item_t item;
    while (xQueueReceive(s_tx_queue, &item, 0) == pdPASS) {
      if (item.timings != NULL) {
        free(item.timings);
      }
    }
    vQueueDelete(s_tx_queue);
    s_tx_queue = NULL;
  }

  if (s_tx_channel != NULL) {
    rmt_disable(s_tx_channel);
    rmt_del_channel(s_tx_channel);
    s_tx_channel = NULL;
  }
  if (s_copy_encoder != NULL) {
    rmt_del_encoder(s_copy_encoder);
    s_copy_encoder = NULL;
  }

  cc1101_strobe(CC1101_SIDLE);

  s_tx_task_handle = NULL;
}

esp_err_t subghz_tx_send_raw(const int32_t *timings, size_t count) {
  if (!s_is_running || timings == NULL || count == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  int32_t *timings_copy = heap_caps_malloc(count * sizeof(int32_t), MALLOC_CAP_DEFAULT);
  if (timings_copy == NULL) {
    ESP_LOGE(TAG, "OOM: Failed to copy TX timings");
    return ESP_ERR_NO_MEM;
  }

  memcpy(timings_copy, timings, count * sizeof(int32_t));

  tx_item_t item = {
    .timings = timings_copy,
    .count = count,
  };

  if (xQueueSend(s_tx_queue, &item, pdMS_TO_TICKS(TX_QUEUE_SEND_MS)) != pdPASS) {
    ESP_LOGE(TAG, "TX Queue Full - Dropping packet");
    free(timings_copy);
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}
