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

#include "subghz_spectrum.h"

#include <string.h>
#include <rom/ets_sys.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "cc1101.h"

static const char *TAG = "SUBGHZ_SPECTRUM";

#define STABILIZATION_DELAY_US  400
#define RSSI_SAMPLE_DELAY_US    20
#define RSSI_SAMPLE_COUNT       3
#define RSSI_MIN_DBM            (-130.0f)
#define YIELD_INTERVAL          16
#define SWEEP_DELAY_MS          5
#define MUTEX_TIMEOUT_MS        10
#define GET_LINE_TIMEOUT_MS     5
#define SPECTRUM_TASK_STACK     4096
#define SPECTRUM_TASK_PRIORITY  1
#define SPECTRUM_TASK_CORE      1

static TaskHandle_t s_spectrum_task_handle = NULL;
static SemaphoreHandle_t s_spectrum_mutex = NULL;
static volatile bool s_stop_requested = false;
static volatile bool s_is_running = false;

static subghz_spectrum_line_t s_latest_line = {0};

static void subghz_spectrum_task(void *pvParameters) {
  s_is_running = true;

  uint32_t center = 0;
  uint32_t span = 0;

  if (xSemaphoreTake(s_spectrum_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    center = s_latest_line.center_freq;
    span = s_latest_line.span_hz;
    xSemaphoreGive(s_spectrum_mutex);
  }

  uint32_t step = span / SPECTRUM_SAMPLES;
  uint32_t start = center - (span / 2);

  ESP_LOGI(TAG, "Spectrum Task Started: Center=%lu, Span=%lu, Step=%lu",
           (unsigned long)center, (unsigned long)span, (unsigned long)step);

  while (!s_stop_requested) {
    float current_sweep[SPECTRUM_SAMPLES];

    for (int i = 0; i < SPECTRUM_SAMPLES; i++) {
      if (s_stop_requested) {
        break;
      }

      uint32_t current_freq = start + ((uint32_t)i * step);

      cc1101_strobe(CC1101_SIDLE);
      cc1101_set_frequency(current_freq);
      cc1101_strobe(CC1101_SRX);

      ets_delay_us(STABILIZATION_DELAY_US);

      float local_max = RSSI_MIN_DBM;
      for (int k = 0; k < RSSI_SAMPLE_COUNT; k++) {
        uint8_t raw = cc1101_read_reg(CC1101_RSSI | 0x40);
        float val = cc1101_convert_rssi(raw);
        if (val > local_max) {
          local_max = val;
        }
        ets_delay_us(RSSI_SAMPLE_DELAY_US);
      }
      current_sweep[i] = local_max;

      if (i % YIELD_INTERVAL == 0) {
        vTaskDelay(1);
      }
    }

    if (xSemaphoreTake(s_spectrum_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
      memcpy(s_latest_line.dbm_values, current_sweep, sizeof(current_sweep));
      s_latest_line.timestamp = esp_timer_get_time();
      s_latest_line.start_freq = start;
      s_latest_line.step_hz = step;
      xSemaphoreGive(s_spectrum_mutex);
    }

    vTaskDelay(pdMS_TO_TICKS(SWEEP_DELAY_MS));
  }

  cc1101_strobe(CC1101_SIDLE);
  s_is_running = false;
  vTaskDelete(NULL);
}

void subghz_spectrum_start(uint32_t center_freq, uint32_t span_hz) {
  if (s_is_running) {
    return;
  }

  if (s_spectrum_mutex == NULL) {
    s_spectrum_mutex = xSemaphoreCreateMutex();
  }

  if (xSemaphoreTake(s_spectrum_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
    s_latest_line.center_freq = center_freq;
    s_latest_line.span_hz = span_hz;
    xSemaphoreGive(s_spectrum_mutex);
  }

  s_stop_requested = false;

  xTaskCreatePinnedToCore(subghz_spectrum_task, "subghz_spectrum",
                          SPECTRUM_TASK_STACK, NULL, SPECTRUM_TASK_PRIORITY,
                          &s_spectrum_task_handle, SPECTRUM_TASK_CORE);
}

void subghz_spectrum_stop(void) {
  if (s_is_running) {
    s_stop_requested = true;
  }
}

bool subghz_spectrum_get_line(subghz_spectrum_line_t *out_line) {
  if (out_line == NULL || s_spectrum_mutex == NULL) {
    return false;
  }

  if (xSemaphoreTake(s_spectrum_mutex, pdMS_TO_TICKS(GET_LINE_TIMEOUT_MS)) == pdTRUE) {
    memcpy(out_line, &s_latest_line, sizeof(subghz_spectrum_line_t));
    xSemaphoreGive(s_spectrum_mutex);
    return true;
  }
  return false;
}
