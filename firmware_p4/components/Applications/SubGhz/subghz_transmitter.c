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
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "cc1101.h"
#include "cc1101_rf_path.h"
#include "led_control.h"
#include "pin_def.h"
#include "resource_mgr.h"
#include "sys_prio.h"

#define RMT_RESOLUTION_HZ     1000000
#define TX_QUEUE_SIZE         10
#define TX_QUEUE_RECV_MS      200
#define TX_QUEUE_SEND_MS      100
#define TX_WAIT_TIMEOUT_MS    2000
#define TX_WAIT_SLICE_MS      50
#define TX_MAX_DURATION_US    120000000ULL
#define TX_MAX_PULSES         8192
#define TX_PA_DBM             10
#define CC_GDO_HIGH_Z         0x2E
#define CC_ASYNC_PACKET       0x32
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
  subghz_tx_config_t config;
  bool apply_config;
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
static atomic_uint s_pending;
static atomic_int s_error;

static void transmit_task(void *context);
static esp_err_t enqueue(const int32_t *timings, size_t count, const subghz_tx_config_t *config);

esp_err_t subghz_tx_init(void) {
  unsigned int expected = SUBGHZ_TX_IDLE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, SUBGHZ_TX_STARTING))
    return expected == SUBGHZ_TX_RUNNING ? ESP_OK : ESP_ERR_INVALID_STATE;
  atomic_store(&s_error, ESP_OK);
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
  atomic_store(&s_error, error);
  s_state = SUBGHZ_TX_IDLE;
  return error;
}

void subghz_tx_get_status(subghz_tx_status_t *out_status) {
  if (out_status == NULL)
    return;
  out_status->is_busy = atomic_load(&s_pending) != 0;
  out_status->error = atomic_load(&s_error);
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
  return enqueue(timings, count, NULL);
}

esp_err_t
subghz_tx_send_configured(const int32_t *timings, size_t count, const subghz_tx_config_t *config) {
  if (config == NULL || cc1101_rf_path_for_frequency(config->frequency_hz) == CC1101_RF_PATH_NONE ||
      config->preset <= CC1101_PRESET_IDLE || config->preset >= CC1101_PRESET_COUNT)
    return ESP_ERR_INVALID_ARG;
  if (config->has_radio_config &&
      (config->radio.preset != config->preset || cc1101_config_validate(&config->radio) != ESP_OK))
    return ESP_ERR_INVALID_ARG;
  return enqueue(timings, count, config);
}

static esp_err_t enqueue(const int32_t *timings, size_t count, const subghz_tx_config_t *config) {
  if (timings == NULL || count == 0 || count > TX_MAX_PULSES)
    return ESP_ERR_INVALID_ARG;
  uint64_t total_us = 0;
  for (size_t i = 0; i < count; ++i) {
    int64_t timing = timings[i];
    uint64_t duration = timing < 0 ? -timing : timing;
    if (duration == 0 || duration > TX_MAX_DURATION_US - total_us)
      return ESP_ERR_INVALID_ARG;
    total_us += duration;
  }
  portENTER_CRITICAL(&s_queue_lock);
  bool can_send = s_state == SUBGHZ_TX_RUNNING;
  if (can_send)
    ++s_senders;
  portEXIT_CRITICAL(&s_queue_lock);
  if (!can_send)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = ESP_ERR_NO_MEM;
  int32_t *copy = heap_caps_malloc(count * sizeof(*copy), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (copy == NULL) {
    ESP_LOGE(TAG, "Cannot allocate TX timings");
    led_signal_error();
    goto cleanup;
  }
  memcpy(copy, timings, count * sizeof(*copy));
  subghz_tx_item_t item = {.timings = copy, .count = count, .apply_config = config != NULL};
  if (config != NULL)
    item.config = *config;
  atomic_fetch_add(&s_pending, 1);
  error = xQueueSend(s_tx_queue, &item, pdMS_TO_TICKS(TX_QUEUE_SEND_MS)) == pdPASS
              ? ESP_OK
              : ESP_ERR_TIMEOUT;
  if (error == ESP_OK)
    copy = NULL;
  else
    atomic_fetch_sub(&s_pending, 1);
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
  size_t halves = 0;
  uint64_t total_us = 0;
  for (size_t i = 0; i < item->count; ++i) {
    int64_t timing = item->timings[i];
    uint32_t duration = timing < 0 ? -timing : timing;
    if (duration == 0)
      return NULL;
    total_us += duration;
    if (total_us > TX_MAX_DURATION_US)
      return NULL;
    halves += (duration + RMT_MAX_DURATION - 1) / RMT_MAX_DURATION;
  }
  size_t words = (halves + 1) / RMT_PULSES_PER_SYMBOL;
  if (words > SIZE_MAX / sizeof(rmt_symbol_word_t))
    return NULL;
  rmt_symbol_word_t *symbols =
      heap_caps_malloc(words * sizeof(*symbols), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (symbols == NULL) {
    ESP_LOGE(TAG, "Cannot allocate TX symbols");
    return NULL;
  }
  memset(symbols, 0, words * sizeof(*symbols));
  size_t half = 0;
  for (size_t i = 0; i < item->count; ++i) {
    int64_t timing = item->timings[i];
    uint32_t remaining = timing < 0 ? -timing : timing;
    while (remaining != 0) {
      uint16_t duration = remaining > RMT_MAX_DURATION ? RMT_MAX_DURATION : remaining;
      if (half % RMT_PULSES_PER_SYMBOL == 0) {
        symbols[half / RMT_PULSES_PER_SYMBOL].duration0 = duration;
        symbols[half / RMT_PULSES_PER_SYMBOL].level0 = timing > 0;
      } else {
        symbols[half / RMT_PULSES_PER_SYMBOL].duration1 = duration;
        symbols[half / RMT_PULSES_PER_SYMBOL].level1 = timing > 0;
      }
      ++half;
      remaining -= duration;
    }
  }
  *out_count = words;
  return symbols;
}

static esp_err_t configure_tx(const subghz_tx_config_t *config) {
  esp_err_t error;
  if (config != NULL) {
    error = config->has_radio_config ? cc1101_set_radio_config(&config->radio, config->frequency_hz)
                                     : cc1101_set_preset(config->preset, config->frequency_hz);
    if (error != ESP_OK)
      return error;
    error = cc1101_set_pa(TX_PA_DBM);
    if (error != ESP_OK)
      return error;
  }
  error = cc1101_set_preset(CC1101_PRESET_IDLE, 0);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_IOCFG0, CC_GDO_HIGH_Z);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_IOCFG2, CC_GDO_HIGH_Z);
  if (error == ESP_OK)
    error = cc1101_write_reg(CC1101_PKTCTRL0, CC_ASYNC_PACKET);
  return error;
}

static void record_failure(esp_err_t error) {
  int expected = ESP_OK;
  if (error != ESP_OK && atomic_compare_exchange_strong(&s_error, &expected, error)) {
    ESP_LOGE(TAG, "Transmission stopped: %s", esp_err_to_name(error));
    led_signal_error();
  }
}

static void transmit_task(void *context) {
  subghz_tx_start_t *start = context;
  bool is_enabled = false;
  bool has_radio_config = false;
  rmt_symbol_word_t *symbols = NULL;
  subghz_tx_item_t item = {0};
  esp_err_t error = ESP_ERR_INVALID_STATE;
  if (s_state != SUBGHZ_TX_STARTING)
    goto ready;
  // Establish legacy raw defaults before enabling the MCU output. Console/host
  // callers may then adjust power/rate; raw jobs preserve those explicit settings.
  error = cc1101_init();
  if (error != ESP_OK)
    goto ready;
  has_radio_config = true;
  error = cc1101_enable_async_mode(TX_DEFAULT_FREQ);
  if (error != ESP_OK)
    goto ready;
  error = configure_tx(NULL);
  if (error != ESP_OK)
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
  unsigned int expected = SUBGHZ_TX_STARTING;
  if (!atomic_compare_exchange_strong(&s_state, &expected, SUBGHZ_TX_RUNNING))
    error = ESP_ERR_INVALID_STATE;
ready:
  if (error != ESP_OK)
    record_failure(error);
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
      error = ESP_ERR_NO_MEM;
      break;
    }
    error = rmt_disable(s_tx_channel);
    if (error != ESP_OK)
      break;
    is_enabled = false;
    error = configure_tx(item.apply_config ? &item.config : NULL);
    if (error != ESP_OK)
      break;
    error = rmt_enable(s_tx_channel);
    if (error != ESP_OK)
      break;
    is_enabled = true;
    ESP_LOGI(TAG,
             "TX queued frame: configured=%u hz=%lu preset=%u pulses=%u",
             item.apply_config,
             (unsigned long)item.config.frequency_hz,
             item.config.preset,
             (unsigned)item.count);
    if (s_state != SUBGHZ_TX_RUNNING) {
      error = ESP_ERR_INVALID_STATE;
      break;
    }
    error = cc1101_enter_tx_mode();
    if (error != ESP_OK)
      break;
    led_set_activity(LED_ACTIVITY_SUBGHZ, true);
    const rmt_transmit_config_t transmit = {0};
    error =
        rmt_transmit(s_tx_channel, s_copy_encoder, symbols, words * sizeof(*symbols), &transmit);
    if (error == ESP_OK) {
      uint64_t duration = 0;
      for (size_t i = 0; i < item.count; ++i) {
        int64_t pulse = item.timings[i];
        duration += pulse < 0 ? -pulse : pulse;
      }
      uint32_t limit_ms = duration / 1000 + TX_WAIT_TIMEOUT_MS;
      for (uint32_t waited = 0; waited < limit_ms; waited += TX_WAIT_SLICE_MS) {
        if (s_state != SUBGHZ_TX_RUNNING) {
          error = ESP_ERR_INVALID_STATE;
          break;
        }
        error = rmt_tx_wait_all_done(s_tx_channel, TX_WAIT_SLICE_MS);
        if (error != ESP_ERR_TIMEOUT)
          break;
      }
    }
    if (error != ESP_OK)
      break;
    error = cc1101_set_preset(CC1101_PRESET_IDLE, 0);
    if (error != ESP_OK)
      break;
    led_set_activity(LED_ACTIVITY_SUBGHZ, false);
    free(symbols);
    symbols = NULL;
    free(item.timings);
    item.timings = NULL;
    atomic_fetch_sub(&s_pending, 1);
  }
  bool is_cancelled = s_state == SUBGHZ_TX_STOPPING;
  if (is_cancelled && error == ESP_ERR_INVALID_STATE)
    error = ESP_OK;
  s_state = SUBGHZ_TX_STOPPING;
  while (has_senders())
    vTaskDelay(pdMS_TO_TICKS(TX_POLL_MS));
  if (!is_cancelled && error == ESP_OK && atomic_load(&s_pending) != 0)
    error = ESP_ERR_INVALID_STATE;
  record_failure(error);
  while ((error = release_channel(&is_enabled)) != ESP_OK) {
    record_failure(error);
    ESP_LOGE(
        TAG, "Retaining TX buffers and grant until cleanup succeeds: %s", esp_err_to_name(error));
    vTaskDelay(pdMS_TO_TICKS(TX_CLEANUP_RETRY_MS));
  }
  while (has_radio_config && (error = cc1101_set_preset(CC1101_PRESET_IDLE, 0)) != ESP_OK) {
    record_failure(error);
    vTaskDelay(pdMS_TO_TICKS(TX_CLEANUP_RETRY_MS));
  }
  led_set_activity(LED_ACTIVITY_SUBGHZ, false);
  free(symbols);
  if (item.timings != NULL) {
    free(item.timings);
    atomic_fetch_sub(&s_pending, 1);
  }
  while (xQueueReceive(s_tx_queue, &item, 0) == pdPASS) {
    free(item.timings);
    atomic_fetch_sub(&s_pending, 1);
  }
  vQueueDelete(s_tx_queue);
  s_tx_queue = NULL;
  resource_release(s_tx_res);
  s_tx_res = RES_HANDLE_NONE;
  s_state = SUBGHZ_TX_IDLE;
  vTaskDelete(NULL);
}
