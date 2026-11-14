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

#include "wifi_capture_queue.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "led_control.h"
#include "sys_prio.h"

#define WRITER_STACK_SIZE 8192
#define WRITER_WAIT_MS    20

static const char *TAG = "WIFI_CAPTURE_QUEUE";
static StaticSemaphore_t s_gate_storage;
static StaticSemaphore_t s_done_storage;
static StaticSemaphore_t s_lifecycle_storage;
static SemaphoreHandle_t s_gate;
static SemaphoreHandle_t s_done;
static SemaphoreHandle_t s_lifecycle;
static TaskHandle_t s_worker;
static wifi_capture_queue_frame_t *s_frames;
static wifi_capture_queue_write_cb_t s_write_cb;
static wifi_capture_queue_stats_t s_stats;
static atomic_uint s_dropped;
static size_t s_head;
static size_t s_tail;
static bool s_is_accepting;

static void writer_task(void *arg);

esp_err_t wifi_capture_queue_start(wifi_capture_queue_write_cb_t write_cb) {
  if (write_cb == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_lifecycle == NULL) {
    s_lifecycle = xSemaphoreCreateMutexStatic(&s_lifecycle_storage);
    s_gate = xSemaphoreCreateMutexStatic(&s_gate_storage);
    s_done = xSemaphoreCreateBinaryStatic(&s_done_storage);
  }
  xSemaphoreTake(s_lifecycle, portMAX_DELAY);
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (s_frames != NULL)
    goto cleanup;
  wifi_capture_queue_frame_t *frames = heap_caps_calloc(
      WIFI_CAPTURE_QUEUE_CAPACITY, sizeof(*frames), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (frames == NULL) {
    ESP_LOGE(TAG, "PSRAM queue allocation failed");
    led_signal_error();
    result = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  xSemaphoreTake(s_done, 0);
  xSemaphoreTake(s_gate, portMAX_DELAY);
  s_frames = frames;
  s_head = 0;
  s_tail = 0;
  memset(&s_stats, 0, sizeof(s_stats));
  atomic_store(&s_dropped, 0);
  s_write_cb = write_cb;
  s_is_accepting = false;
  if (xTaskCreatePinnedToCoreWithCaps(writer_task,
                                      "pcap_writer",
                                      WRITER_STACK_SIZE,
                                      NULL,
                                      SYS_PRIO_SERVICE_LO,
                                      &s_worker,
                                      SYS_CORE_RADIO,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    free(s_frames);
    s_frames = NULL;
    s_worker = NULL;
    result = ESP_ERR_NO_MEM;
    ESP_LOGE(TAG, "Writer task allocation failed");
    led_signal_error();
  } else {
    s_is_accepting = true;
    result = ESP_OK;
  }
  xSemaphoreGive(s_gate);
cleanup:
  xSemaphoreGive(s_lifecycle);
  return result;
}

esp_err_t wifi_capture_queue_push(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > WIFI_CAPTURE_QUEUE_FRAME_MAX)
    return ESP_ERR_INVALID_ARG;
  if (s_gate == NULL)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(s_gate, 0) != pdTRUE) {
    atomic_fetch_add(&s_dropped, 1);
    return ESP_ERR_TIMEOUT;
  }
  esp_err_t result = ESP_OK;
  if (!s_is_accepting || s_frames == NULL || s_stats.error != ESP_OK) {
    if (s_is_accepting && s_stats.error != ESP_OK)
      atomic_fetch_add(&s_dropped, 1);
    result = ESP_ERR_INVALID_STATE;
  } else if (s_stats.pending == WIFI_CAPTURE_QUEUE_CAPACITY) {
    atomic_fetch_add(&s_dropped, 1);
    result = ESP_ERR_NO_MEM;
  } else {
    wifi_capture_queue_frame_t *frame = &s_frames[s_tail];
    gettimeofday(&frame->received_at, NULL);
    frame->len = (uint16_t)len;
    memcpy(frame->data, data, len);
    s_tail = (s_tail + 1) % WIFI_CAPTURE_QUEUE_CAPACITY;
    s_stats.pending++;
    if (s_stats.pending > s_stats.peak)
      s_stats.peak = s_stats.pending;
    xTaskNotifyGive(s_worker);
  }
  xSemaphoreGive(s_gate);
  return result;
}

esp_err_t wifi_capture_queue_stop(void) {
  if (s_lifecycle == NULL)
    return ESP_OK;
  xSemaphoreTake(s_lifecycle, portMAX_DELAY);
  xSemaphoreTake(s_gate, portMAX_DELAY);
  bool is_running = s_frames != NULL;
  s_is_accepting = false;
  if (is_running)
    xTaskNotifyGive(s_worker);
  xSemaphoreGive(s_gate);
  if (is_running)
    xSemaphoreTake(s_done, portMAX_DELAY);
  xSemaphoreTake(s_gate, portMAX_DELAY);
  free(s_frames);
  s_frames = NULL;
  s_worker = NULL;
  esp_err_t result = s_stats.error;
  xSemaphoreGive(s_gate);
  xSemaphoreGive(s_lifecycle);
  return result;
}

void wifi_capture_queue_get_stats(wifi_capture_queue_stats_t *out_stats) {
  memset(out_stats, 0, sizeof(*out_stats));
  if (s_gate == NULL)
    return;
  xSemaphoreTake(s_gate, portMAX_DELAY);
  *out_stats = s_stats;
  out_stats->dropped = atomic_load(&s_dropped);
  xSemaphoreGive(s_gate);
}

static void writer_task(void *arg) {
  (void)arg;
  uint32_t reported_drops = 0;
  for (;;) {
    xSemaphoreTake(s_gate, portMAX_DELAY);
    wifi_capture_queue_frame_t *frame = s_stats.pending ? &s_frames[s_head] : NULL;
    bool is_done = !s_is_accepting && frame == NULL;
    esp_err_t error = s_stats.error;
    xSemaphoreGive(s_gate);
    uint32_t dropped = atomic_load(&s_dropped);
    if (dropped != reported_drops) {
      if (reported_drops == 0) {
        ESP_LOGW(TAG, "Capture queue overflow/contention; frames lost");
        led_signal_warning();
      }
      reported_drops = dropped;
    }
    if (is_done)
      break;
    if (frame == NULL) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WRITER_WAIT_MS));
      continue;
    }
    esp_err_t result = error == ESP_OK ? s_write_cb(frame) : error;
    xSemaphoreTake(s_gate, portMAX_DELAY);
    if (result != ESP_OK) {
      s_stats.error = result;
      s_stats.discarded++;
    }
    s_head = (s_head + 1) % WIFI_CAPTURE_QUEUE_CAPACITY;
    s_stats.pending--;
    xSemaphoreGive(s_gate);
    if (error == ESP_OK && result != ESP_OK) {
      ESP_LOGE(
          TAG, "SD writer failed: %s; pending frames will be discarded", esp_err_to_name(result));
      led_signal_error();
    }
  }
  xSemaphoreGive(s_done);
  vTaskDeleteWithCaps(NULL);
}
