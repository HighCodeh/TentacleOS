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

#include "subghz_spectrum.h"

#include <string.h>
#include <stdatomic.h>
#include "rom/ets_sys.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "freertos/semphr.h"

#include "cc1101.h"
#include "resource_mgr.h"

static const char *TAG = "SUBGHZ_SPECTRUM";

#define STABILIZATION_DELAY_US  400
#define RSSI_SAMPLE_DELAY_US    20
#define RSSI_SAMPLE_COUNT       3
#define RSSI_MIN_DBM            (-130.0f)
#define YIELD_INTERVAL          1
#define SWEEP_DELAY_MS          15
#define MUTEX_TIMEOUT_MS        10
#define GET_LINE_TIMEOUT_MS     5
#define SPECTRUM_TASK_STACK     4096
#define SPECTRUM_CENTER_DIVISOR 2
#define SPECTRUM_TASK_PRIORITY  SYS_PRIO_BACKGROUND_LO
#define SPECTRUM_TASK_CORE      SYS_CORE_RADIO

typedef enum {
  SUBGHZ_SPECTRUM_IDLE = 0,
  SUBGHZ_SPECTRUM_RUNNING,
  SUBGHZ_SPECTRUM_STOPPING,
} subghz_spectrum_state_t;

static _Atomic(SemaphoreHandle_t) s_spectrum_mutex;
static atomic_uint s_state;
static res_handle_t s_grant;

static subghz_spectrum_line_t s_latest_line = {0};

static void subghz_spectrum_task(void *pvParameters) {
  uint32_t center = 0;
  uint32_t span = 0;

  if (xSemaphoreTake(s_spectrum_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE)
    goto cleanup;
  center = s_latest_line.center_freq;
  span = s_latest_line.span_hz;
  xSemaphoreGive(s_spectrum_mutex);

  uint32_t step = span / SPECTRUM_SAMPLES;
  uint32_t start = center - (span / SPECTRUM_CENTER_DIVISOR);

  ESP_LOGI(TAG,
           "Spectrum Task Started: Center=%lu, Span=%lu, Step=%lu",
           (unsigned long)center,
           (unsigned long)span,
           (unsigned long)step);

  while (s_state == SUBGHZ_SPECTRUM_RUNNING) {
    float current_sweep[SPECTRUM_SAMPLES];

    for (int i = 0; i < SPECTRUM_SAMPLES; i++) {
      if (s_state != SUBGHZ_SPECTRUM_RUNNING) {
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

    if (s_state != SUBGHZ_SPECTRUM_RUNNING)
      break;

    if (xSemaphoreTake(s_spectrum_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
      memcpy(s_latest_line.dbm_values, current_sweep, sizeof(current_sweep));
      s_latest_line.timestamp = esp_timer_get_time();
      s_latest_line.start_freq = start;
      s_latest_line.step_hz = step;
      xSemaphoreGive(s_spectrum_mutex);
    }

    vTaskDelay(pdMS_TO_TICKS(SWEEP_DELAY_MS));
  }

cleanup:
  cc1101_strobe(CC1101_SIDLE);
  resource_release(s_grant);
  s_grant = RES_HANDLE_NONE;
  s_state = SUBGHZ_SPECTRUM_IDLE;
  vTaskDelete(NULL);
}

void subghz_spectrum_start(uint32_t center_freq, uint32_t span_hz) {
  if (span_hz < SPECTRUM_SAMPLES || center_freq < span_hz / SPECTRUM_CENTER_DIVISOR)
    return;
  unsigned int expected = SUBGHZ_SPECTRUM_IDLE;
  if (!atomic_compare_exchange_strong(&s_state, &expected, SUBGHZ_SPECTRUM_RUNNING))
    return;
  res_request_t request = {
      .id = RES_SUBGHZ, .lane = RES_LANE_MAIN, .owner_kind = RES_OWNER_UI, .allow_preempt = false};
  if (resource_acquire(&request, &s_grant) != ESP_OK) {
    s_state = SUBGHZ_SPECTRUM_IDLE;
    return;
  }

  if (s_spectrum_mutex == NULL) {
    s_spectrum_mutex = xSemaphoreCreateMutex();
  }

  if (s_spectrum_mutex == NULL ||
      xSemaphoreTake(s_spectrum_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
    ESP_LOGE(TAG, "Cannot initialize spectrum snapshot");
    resource_release(s_grant);
    s_grant = RES_HANDLE_NONE;
    s_state = SUBGHZ_SPECTRUM_IDLE;
    return;
  }
  memset(&s_latest_line, 0, sizeof(s_latest_line));
  s_latest_line.center_freq = center_freq;
  s_latest_line.span_hz = span_hz;
  xSemaphoreGive(s_spectrum_mutex);

  BaseType_t created = xTaskCreatePinnedToCore(subghz_spectrum_task,
                                               "subghz_spectrum",
                                               SPECTRUM_TASK_STACK,
                                               NULL,
                                               SPECTRUM_TASK_PRIORITY,
                                               NULL,
                                               SPECTRUM_TASK_CORE);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Cannot start spectrum worker");
    resource_release(s_grant);
    s_grant = RES_HANDLE_NONE;
    s_state = SUBGHZ_SPECTRUM_IDLE;
  }
}

void subghz_spectrum_stop(void) {
  unsigned int expected = SUBGHZ_SPECTRUM_RUNNING;
  atomic_compare_exchange_strong(&s_state, &expected, SUBGHZ_SPECTRUM_STOPPING);
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
