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

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "cc1101.h"
#include "led_control.h"
#include "pin_def.h"
#include "resource_mgr.h"
#include "sys_prio.h"

#define RMT_RESOLUTION_HZ     1000000
#define TX_QUEUE_SIZE         10
#define TX_QUEUE_RECV_MS      200
#define TX_QUEUE_SEND_MS      100
#define TX_WAIT_TIMEOUT_MS    2000
#define TX_TASK_STACK_SIZE    4096
#define TX_POLL_MS            10
#define TX_STOP_TIMEOUT_MS    2500
#define TX_CLEANUP_RETRY_MS   100
#define RMT_MEM_BLOCK_SYMBOLS 64
#define RMT_TRANS_QUEUE_DEPTH 4
#define RMT_MAX_DURATION      32767
#define RMT_PULSES_PER_SYMBOL 2
#define TX_DEFAULT_FREQ       433920000

typedef enum {
  SUBGHZ_TX_IDLE = 0,
  SUBGHZ_TX_STARTING,
  SUBGHZ_TX_RUNNING,
  SUBGHZ_TX_STOPPING,
} subghz_tx_state_t;

typedef struct {
  int32_t *timings;
  size_t count;
} subghz_tx_item_t;

typedef struct {
  SemaphoreHandle_t ready;
  esp_err_t error;
} subghz_tx_start_t;

static const char *TAG = "SUBGHZ_TX";
static res_handle_t s_tx_res;
static rmt_channel_handle_t s_tx_channel;
static rmt_encoder_handle_t s_copy_encoder;
static QueueHandle_t s_tx_queue;
static atomic_uint s_state;
static portMUX_TYPE s_queue_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_senders;

static void transmit_task(void *context);

esp_err_t subghz_tx_init(void) {
  unsigned int expected = SUBGHZ_TX_IDLE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, SUBGHZ_TX_STARTING))
    return expected == SUBGHZ_TX_RUNNING ? ESP_OK : ESP_ERR_INVALID_STATE;
  subghz_tx_start_t start = {.ready = xSemaphoreCreateBinary()};
  esp_err_t error = ESP_ERR_NO_MEM;
  if (start.ready == NULL) {
    ESP_LOGE(TAG, "Cannot allocate TX startup signal");
    goto cleanup;
  }
  const res_request_t request = {
      .id = RES_SUBGHZ, .lane = RES_LANE_MAIN, .owner_kind = RES_OWNER_UI, .allow_preempt = false};
  error = resource_acquire(&request, &s_tx_res);
  if (error != ESP_OK)
    goto cleanup;
  s_tx_queue = xQueueCreate(TX_QUEUE_SIZE, sizeof(subghz_tx_item_t));
  if (s_tx_queue == NULL) {
    ESP_LOGE(TAG, "Cannot allocate TX queue");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  if (xTaskCreatePinnedToCore(transmit_task,
                              "subghz_tx",
                              TX_TASK_STACK_SIZE,
                              &start,
                              SYS_PRIO_SERVICE_HI,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    ESP_LOGE(TAG, "Cannot start TX worker");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  xSemaphoreTake(start.ready, portMAX_DELAY);
  error = start.error;
  vSemaphoreDelete(start.ready);
  return error;
cleanup:
  if (s_tx_queue != NULL) {
    vQueueDelete(s_tx_queue);
    s_tx_queue = NULL;
  }
  if (s_tx_res != RES_HANDLE_NONE) {
    resource_release(s_tx_res);
    s_tx_res = RES_HANDLE_NONE;
  }
  if (start.ready != NULL)
    vSemaphoreDelete(start.ready);
  s_state = SUBGHZ_TX_IDLE;
  return error;
}

void subghz_tx_stop(void) {
  unsigned int state = atomic_load(&s_state);
  while (state == SUBGHZ_TX_RUNNING || state == SUBGHZ_TX_STARTING) {
    if (atomic_compare_exchange_weak(&s_state, &state, SUBGHZ_TX_STOPPING))
      break;
  }
  for (uint32_t waited = 0; s_state != SUBGHZ_TX_IDLE && waited < TX_STOP_TIMEOUT_MS;
       waited += TX_POLL_MS)
    vTaskDelay(pdMS_TO_TICKS(TX_POLL_MS));
}

esp_err_t subghz_tx_send_raw(const int32_t *timings, size_t count) {
  if (timings == NULL || count == 0 || count > SIZE_MAX / sizeof(*timings))
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_queue_lock);
  bool can_send = s_state == SUBGHZ_TX_RUNNING;
  if (can_send)
    ++s_senders;
  portEXIT_CRITICAL(&s_queue_lock);
  if (!can_send)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = ESP_ERR_NO_MEM;
  int32_t *copy = malloc(count * sizeof(*copy));
  if (copy == NULL) {
    ESP_LOGE(TAG, "Cannot allocate TX timings");
    led_signal_error();
    goto cleanup;
  }
  memcpy(copy, timings, count * sizeof(*copy));
  subghz_tx_item_t item = {.timings = copy, .count = count};
  error = xQueueSend(s_tx_queue, &item, pdMS_TO_TICKS(TX_QUEUE_SEND_MS)) == pdPASS
              ? ESP_OK
              : ESP_ERR_TIMEOUT;
  if (error == ESP_OK)
    copy = NULL;
cleanup:
  free(copy);
  portENTER_CRITICAL(&s_queue_lock);
  --s_senders;
  portEXIT_CRITICAL(&s_queue_lock);
  return error;
}

static bool has_senders(void) {
  portENTER_CRITICAL(&s_queue_lock);
  bool has_pending = s_senders != 0;
  portEXIT_CRITICAL(&s_queue_lock);
  return has_pending;
}

static esp_err_t release_channel(bool *is_enabled) {
  if (s_tx_channel != NULL) {
    esp_err_t error = *is_enabled ? rmt_disable(s_tx_channel) : ESP_OK;
    if (error != ESP_OK)
      return error;
    *is_enabled = false;
    error = rmt_del_channel(s_tx_channel);
    if (error != ESP_OK)
      return error;
    s_tx_channel = NULL;
  }
  if (s_copy_encoder != NULL) {
    esp_err_t error = rmt_del_encoder(s_copy_encoder);
    if (error != ESP_OK)
      return error;
    s_copy_encoder = NULL;
  }
  return ESP_OK;
}

static rmt_symbol_word_t *make_symbols(const subghz_tx_item_t *item, size_t *out_count) {
  size_t words = item->count / RMT_PULSES_PER_SYMBOL + item->count % RMT_PULSES_PER_SYMBOL;
  if (words > SIZE_MAX / sizeof(rmt_symbol_word_t))
    return NULL;
  rmt_symbol_word_t *symbols = calloc(words, sizeof(*symbols));
  if (symbols == NULL) {
    ESP_LOGE(TAG, "Cannot allocate TX symbols");
    return NULL;
  }
  for (size_t i = 0; i < item->count; ++i) {
    int32_t timing = item->timings[i];
    uint32_t duration = timing > 0 ? timing : -(int64_t)timing;
    if (duration > RMT_MAX_DURATION)
      duration = RMT_MAX_DURATION;
    if (i % RMT_PULSES_PER_SYMBOL == 0) {
      symbols[i / RMT_PULSES_PER_SYMBOL].duration0 = duration;
      symbols[i / RMT_PULSES_PER_SYMBOL].level0 = timing > 0;
    } else {
      symbols[i / RMT_PULSES_PER_SYMBOL].duration1 = duration;
      symbols[i / RMT_PULSES_PER_SYMBOL].level1 = timing > 0;
    }
  }
  *out_count = words;
  return symbols;
}

static void transmit_task(void *context) {
  subghz_tx_start_t *start = context;
  bool is_enabled = false;
  rmt_symbol_word_t *symbols = NULL;
  subghz_tx_item_t item = {0};
  esp_err_t error = ESP_ERR_INVALID_STATE;
  if (s_state != SUBGHZ_TX_STARTING)
    goto ready;
  const rmt_tx_channel_config_t config = {.clk_src = RMT_CLK_SRC_DEFAULT,
                                          .resolution_hz = RMT_RESOLUTION_HZ,
                                          .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
                                          .trans_queue_depth = RMT_TRANS_QUEUE_DEPTH,
                                          .gpio_num = GPIO_CC1101_GDO0_PIN};
  error = rmt_new_tx_channel(&config, &s_tx_channel);
  if (error != ESP_OK)
    goto ready;
  const rmt_copy_encoder_config_t encoder = {};
  error = rmt_new_copy_encoder(&encoder, &s_copy_encoder);
  if (error != ESP_OK)
    goto ready;
  error = rmt_enable(s_tx_channel);
  if (error != ESP_OK)
    goto ready;
  is_enabled = true;
  if (s_state != SUBGHZ_TX_STARTING) {
    error = ESP_ERR_INVALID_STATE;
    goto ready;
  }
  cc1101_enable_async_mode(TX_DEFAULT_FREQ);
  cc1101_strobe(CC1101_SIDLE);
  unsigned int expected = SUBGHZ_TX_STARTING;
  if (!atomic_compare_exchange_strong(&s_state, &expected, SUBGHZ_TX_RUNNING))
    error = ESP_ERR_INVALID_STATE;
ready:
  start->error = error;
  xSemaphoreGive(start->ready);
  // The caller may now destroy the startup signal and its stack context.
  start = NULL;
  while (error == ESP_OK && s_state == SUBGHZ_TX_RUNNING) {
    if (xQueueReceive(s_tx_queue, &item, pdMS_TO_TICKS(TX_QUEUE_RECV_MS)) != pdPASS)
      continue;
    size_t words = 0;
    symbols = make_symbols(&item, &words);
    if (symbols == NULL) {
      free(item.timings);
      item.timings = NULL;
      continue;
    }
    cc1101_enter_tx_mode();
    const rmt_transmit_config_t transmit = {0};
    error =
        rmt_transmit(s_tx_channel, s_copy_encoder, symbols, words * sizeof(*symbols), &transmit);
    if (error == ESP_OK)
      error = rmt_tx_wait_all_done(s_tx_channel, TX_WAIT_TIMEOUT_MS);
    if (error != ESP_OK)
      break;
    free(symbols);
    symbols = NULL;
    free(item.timings);
    item.timings = NULL;
    cc1101_strobe(CC1101_SIDLE);
  }
  s_state = SUBGHZ_TX_STOPPING;
  while (has_senders())
    vTaskDelay(pdMS_TO_TICKS(TX_POLL_MS));
  cc1101_strobe(CC1101_SIDLE);
  while ((error = release_channel(&is_enabled)) != ESP_OK) {
    ESP_LOGE(
        TAG, "Retaining TX buffers and grant until cleanup succeeds: %s", esp_err_to_name(error));
    vTaskDelay(pdMS_TO_TICKS(TX_CLEANUP_RETRY_MS));
  }
  free(symbols);
  free(item.timings);
  while (xQueueReceive(s_tx_queue, &item, 0) == pdPASS)
    free(item.timings);
  vQueueDelete(s_tx_queue);
  s_tx_queue = NULL;
  resource_release(s_tx_res);
  s_tx_res = RES_HANDLE_NONE;
  s_state = SUBGHZ_TX_IDLE;
  vTaskDelete(NULL);
}
