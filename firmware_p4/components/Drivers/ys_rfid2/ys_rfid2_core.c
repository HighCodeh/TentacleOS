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

#include "ys_rfid2.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "pin_def.h"
#include "ys_rfid2_hal_uart.h"
#include "ys_rfid2_parser.h"

static const char *TAG = "YS_RFID2";

#define RFID_TASK_STACK_SIZE  4096
#define RFID_TASK_PRIORITY    5
#define RFID_READ_TIMEOUT_MS  100
#define RFID_DEFAULT_BAUD     9600
#define RFID_DEFAULT_DEBOUNCE_MS    1000
#define RFID_DEFAULT_REMOVAL_MS     2000

typedef struct {
  ys_rfid2_config_t config;
  ys_rfid2_state_t state;
  ys_rfid2_event_cb_t cb;
  void *ctx;
  TaskHandle_t task;
  volatile bool is_running;
  ys_rfid2_event_t last_event;
  bool has_last_card;
  int64_t last_card_time_ms;
  SemaphoreHandle_t mutex;
} ys_rfid2_driver_t;

static ys_rfid2_driver_t s_rfid = {0};

static void reader_task(void *pvParameters);
static void handle_card_detected(const ys_rfid2_raw_data_t *raw, int64_t now_ms);
static void check_card_removal(int64_t now_ms);
static int64_t get_time_ms(void);

ys_rfid2_config_t ys_rfid2_default_config(void) {
  ys_rfid2_config_t config = {
      .uart_port = UART_NUM_2,
      .baud_rate = RFID_DEFAULT_BAUD,
      .tx_pin = GPIO_RFID_UART_TX_PIN,
      .rx_pin = GPIO_RFID_UART_RX_PIN,
      .debounce_ms = RFID_DEFAULT_DEBOUNCE_MS,
      .removal_timeout_ms = RFID_DEFAULT_REMOVAL_MS,
  };
  return config;
}

esp_err_t ys_rfid2_init(const ys_rfid2_config_t *config) {
  if (s_rfid.state != YS_RFID2_STATE_UNINITIALIZED) {
    return ESP_ERR_INVALID_STATE;
  }

  if (config != NULL) {
    s_rfid.config = *config;
  } else {
    s_rfid.config = ys_rfid2_default_config();
  }

  s_rfid.mutex = xSemaphoreCreateMutex();
  if (s_rfid.mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }

  ys_rfid2_hal_uart_config_t hal_config = {
      .port = s_rfid.config.uart_port,
      .baud_rate = s_rfid.config.baud_rate,
      .tx_pin = s_rfid.config.tx_pin,
      .rx_pin = s_rfid.config.rx_pin,
  };

  esp_err_t ret = ys_rfid2_hal_uart_init(&hal_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init HAL UART: %s", esp_err_to_name(ret));
    vSemaphoreDelete(s_rfid.mutex);
    s_rfid.mutex = NULL;
    return ret;
  }

  s_rfid.state = YS_RFID2_STATE_IDLE;
  s_rfid.has_last_card = false;

  ESP_LOGI(TAG, "Initialized — port: %d, baud: %d",
      s_rfid.config.uart_port, s_rfid.config.baud_rate);

  return ESP_OK;
}

esp_err_t ys_rfid2_deinit(void) {
  if (s_rfid.state == YS_RFID2_STATE_UNINITIALIZED) {
    return ESP_ERR_INVALID_STATE;
  }

  if (s_rfid.state == YS_RFID2_STATE_SCANNING) {
    ys_rfid2_stop();
  }

  ys_rfid2_hal_uart_deinit();

  if (s_rfid.mutex != NULL) {
    vSemaphoreDelete(s_rfid.mutex);
    s_rfid.mutex = NULL;
  }

  s_rfid.state = YS_RFID2_STATE_UNINITIALIZED;

  ESP_LOGI(TAG, "Deinitialized");
  return ESP_OK;
}

esp_err_t ys_rfid2_start(ys_rfid2_event_cb_t cb, void *ctx) {
  if (cb == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_rfid.state != YS_RFID2_STATE_IDLE) {
    return ESP_ERR_INVALID_STATE;
  }

  s_rfid.cb = cb;
  s_rfid.ctx = ctx;
  s_rfid.is_running = true;
  s_rfid.has_last_card = false;

  ys_rfid2_parser_reset();
  ys_rfid2_hal_uart_flush();

  BaseType_t ret = xTaskCreate(
      reader_task, "ys_rfid2", RFID_TASK_STACK_SIZE,
      NULL, RFID_TASK_PRIORITY, &s_rfid.task);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create reader task");
    s_rfid.is_running = false;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Scanning started");
  return ESP_OK;
}

void ys_rfid2_stop(void) {
  if (!s_rfid.is_running) {
    return;
  }

  s_rfid.is_running = false;

  while (s_rfid.task != NULL) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  s_rfid.cb = NULL;
  s_rfid.ctx = NULL;

  ESP_LOGI(TAG, "Scanning stopped");
}

ys_rfid2_state_t ys_rfid2_get_state(void) {
  return s_rfid.state;
}

esp_err_t ys_rfid2_get_last_card(ys_rfid2_event_t *out_event) {
  if (out_event == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_rfid.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  if (!s_rfid.has_last_card) {
    xSemaphoreGive(s_rfid.mutex);
    return ESP_ERR_NOT_FOUND;
  }

  *out_event = s_rfid.last_event;
  xSemaphoreGive(s_rfid.mutex);

  return ESP_OK;
}

static void reader_task(void *pvParameters) {
  (void)pvParameters;

  xSemaphoreTake(s_rfid.mutex, portMAX_DELAY);
  s_rfid.state = YS_RFID2_STATE_SCANNING;
  xSemaphoreGive(s_rfid.mutex);

  uint8_t rx_byte;

  while (s_rfid.is_running) {
    int64_t now_ms = get_time_ms();
    int len = ys_rfid2_hal_uart_read(&rx_byte, 1, RFID_READ_TIMEOUT_MS);

    if (len > 0) {
      ys_rfid2_raw_data_t raw = {0};
      if (ys_rfid2_parser_feed(rx_byte, &raw)) {
        handle_card_detected(&raw, now_ms);
      }
    } else {
      check_card_removal(now_ms);
    }
  }

  xSemaphoreTake(s_rfid.mutex, portMAX_DELAY);
  s_rfid.state = YS_RFID2_STATE_IDLE;
  s_rfid.task = NULL;
  xSemaphoreGive(s_rfid.mutex);

  vTaskDelete(NULL);
}

static void handle_card_detected(const ys_rfid2_raw_data_t *raw, int64_t now_ms) {
  xSemaphoreTake(s_rfid.mutex, portMAX_DELAY);

  bool is_same = s_rfid.has_last_card &&
      (strcmp(s_rfid.last_event.raw.id_str, raw->id_str) == 0);

  bool is_debounced = is_same &&
      (now_ms - s_rfid.last_card_time_ms < (int64_t)s_rfid.config.debounce_ms);

  if (is_debounced) {
    s_rfid.last_card_time_ms = now_ms;
    xSemaphoreGive(s_rfid.mutex);
    return;
  }

  s_rfid.last_event.type = YS_RFID2_EVENT_CARD_DETECTED;
  s_rfid.last_event.raw = *raw;
  s_rfid.last_event.timestamp_ms = now_ms;
  s_rfid.last_card_time_ms = now_ms;
  s_rfid.has_last_card = true;

  ys_rfid2_event_cb_t cb = s_rfid.cb;
  void *ctx = s_rfid.ctx;
  xSemaphoreGive(s_rfid.mutex);

  if (cb != NULL) {
    cb(&s_rfid.last_event, ctx);
  }
}

static void check_card_removal(int64_t now_ms) {
  xSemaphoreTake(s_rfid.mutex, portMAX_DELAY);

  if (!s_rfid.has_last_card) {
    xSemaphoreGive(s_rfid.mutex);
    return;
  }

  if (now_ms - s_rfid.last_card_time_ms <= (int64_t)s_rfid.config.removal_timeout_ms) {
    xSemaphoreGive(s_rfid.mutex);
    return;
  }

  ys_rfid2_event_t removal_event = {
      .type = YS_RFID2_EVENT_CARD_REMOVED,
      .raw = s_rfid.last_event.raw,
      .timestamp_ms = now_ms,
  };

  s_rfid.has_last_card = false;

  ys_rfid2_event_cb_t cb = s_rfid.cb;
  void *ctx = s_rfid.ctx;
  xSemaphoreGive(s_rfid.mutex);

  if (cb != NULL) {
    cb(&removal_event, ctx);
  }
}

static int64_t get_time_ms(void) {
  return esp_timer_get_time() / 1000;
}
