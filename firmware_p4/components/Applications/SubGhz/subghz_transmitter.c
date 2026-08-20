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

#include "subghz_transmitter.h"

#include "led_control.h"

#include <stdlib.h>
#include <string.h>

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "freertos/queue.h"

#include "cc1101.h"
#include "pin_def.h"

static const char *TAG = "SUBGHZ_TX";

#define RMT_RESOLUTION_HZ     1000000
#define TX_QUEUE_SIZE         10
#define TX_QUEUE_RECV_MS      200
#define TX_QUEUE_SEND_MS      100
#define TX_WAIT_TIMEOUT_MS    2000
#define TX_TASK_STACK_SIZE    4096
#define TX_TASK_PRIORITY      SYS_PRIO_SERVICE_HI
#define TX_TASK_CORE          SYS_CORE_RADIO
#define TX_STOP_DELAY_MS      100
#define RMT_MEM_BLOCK_SYMBOLS 64
#define RMT_TRANS_QUEUE_DEPTH 4
#define RMT_MAX_DURATION      32767
#define TX_DEFAULT_FREQ       433920000

typedef struct {
  int32_t *timings;
  size_t count;
} subghz_tx_item_t;

static rmt_channel_handle_t s_tx_channel = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;
static QueueHandle_t s_tx_queue = NULL;
static TaskHandle_t s_tx_task_handle = NULL;
static volatile bool s_is_running = false;

static void subghz_tx_task(void *pvParameters) {
  subghz_tx_item_t item;

  ESP_LOGI(TAG, "TX Task Started");

  while (s_is_running) {
    if (xQueueReceive(s_tx_queue, &item, pdMS_TO_TICKS(TX_QUEUE_RECV_MS)) != pdPASS) {
      continue;
    }

    ESP_LOGI(TAG, "Processing TX item: %d symbols", (int)item.count);

    size_t num_words = (item.count + 1) / 2;
    rmt_symbol_word_t *symbols = malloc(num_words * sizeof(rmt_symbol_word_t));

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

    esp_err_t tx_err = rmt_transmit(
        s_tx_channel, s_copy_encoder, symbols, num_words * sizeof(rmt_symbol_word_t), &tx_config);
    if (tx_err != ESP_OK) {
      ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(tx_err));
      free(symbols);
      cc1101_strobe(CC1101_SIDLE);
      if (item.timings != NULL) {
        free(item.timings);
      }
      continue;
    }

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
      .gpio_num = GPIO_CC1101_GDO0_PIN,
      .flags.invert_out = false,
  };
  esp_err_t err = rmt_new_tx_channel(&tx_channel_cfg, &s_tx_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_tx_channel failed: %s", esp_err_to_name(err));
    return err;
  }

  rmt_copy_encoder_config_t copy_encoder_cfg = {};
  err = rmt_new_copy_encoder(&copy_encoder_cfg, &s_copy_encoder);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(err));
    rmt_del_channel(s_tx_channel);
    s_tx_channel = NULL;
    return err;
  }

  err = rmt_enable(s_tx_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable failed: %s", esp_err_to_name(err));
    rmt_del_encoder(s_copy_encoder);
    rmt_del_channel(s_tx_channel);
    s_copy_encoder = NULL;
    s_tx_channel = NULL;
    return err;
  }

  cc1101_enable_async_mode(TX_DEFAULT_FREQ);
  cc1101_strobe(CC1101_SIDLE);

  s_tx_queue = xQueueCreate(TX_QUEUE_SIZE, sizeof(subghz_tx_item_t));
  if (s_tx_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create TX Queue");
    return ESP_ERR_NO_MEM;
  }

  s_is_running = true;
  xTaskCreatePinnedToCore(subghz_tx_task,
                          "subghz_tx",
                          TX_TASK_STACK_SIZE,
                          NULL,
                          TX_TASK_PRIORITY,
                          &s_tx_task_handle,
                          TX_TASK_CORE);
  return ESP_OK;
}

void subghz_tx_stop(void) {
  if (s_is_running == false) {
    return;
  }

  ESP_LOGI(TAG, "Stopping SubGhz Transmitter");
  s_is_running = false;

  vTaskDelay(pdMS_TO_TICKS(TX_STOP_DELAY_MS));

  if (s_tx_queue != NULL) {
    subghz_tx_item_t item;
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
  if (s_is_running == false || timings == NULL || count == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  int32_t *timings_copy = malloc(count * sizeof(int32_t));
  if (timings_copy == NULL) {
    ESP_LOGE(TAG, "OOM: Failed to copy TX timings");
    led_signal_error();
    return ESP_ERR_NO_MEM;
  }

  memcpy(timings_copy, timings, count * sizeof(int32_t));

  subghz_tx_item_t item = {
      .timings = timings_copy,
      .count = count,
  };

  if (xQueueSend(s_tx_queue, &item, pdMS_TO_TICKS(TX_QUEUE_SEND_MS)) != pdPASS) {
    ESP_LOGE(TAG, "TX Queue Full - Dropping packet");
    free(timings_copy);
    led_signal_error();
    return ESP_ERR_TIMEOUT;
  }

  return ESP_OK;
}
