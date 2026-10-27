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

#include "wifi_quick_scan.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "sys_prio.h"
#include "wifi_quick_scan_core.h"
#include "wifi_service.h"

#define QUICK_QUEUE_LENGTH   64
#define QUICK_STACK_SIZE     4096
#define QUICK_DURATION_SHORT 30
#define QUICK_DURATION_LONG  60
#define QUICK_MS_PER_SECOND  1000
#define QUICK_US_PER_MS      1000
#define QUICK_FCS_LENGTH     4
#define QUICK_MAC_LEN        SPI_WIFI_QUICK_MAC_LEN
#define QUICK_MAC_GROUP_BIT  0x01

static const char *TAG = "WIFI_QUICK_SCAN";

static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_queue;
static wifi_quick_scan_core_t *s_core;
static spi_wifi_quick_status_t s_status;
static spi_wifi_quick_config_t s_config;
static TaskHandle_t s_task;
static uint8_t s_whitelist[WIFI_QUICK_SCAN_WHITELIST_MAX][QUICK_MAC_LEN];
static size_t s_whitelist_count;
static atomic_bool s_cancel;
static atomic_uint s_dropped;
static int64_t s_start_us;

static void capture_task(void *ctx);
static void receive_frame(void *buf, wifi_promiscuous_pkt_type_t type);
static bool capture_tick(void *ctx);

esp_err_t wifi_quick_scan_init(void) {
  if (s_mutex != NULL) {
    return ESP_OK;
  }
  esp_err_t err = ESP_ERR_NO_MEM;
  s_core = heap_caps_calloc(1, sizeof(*s_core), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_core == NULL) {
    ESP_LOGE(TAG, "Failed to allocate analysis records");
    goto cleanup;
  }
  s_queue = xQueueCreate(QUICK_QUEUE_LENGTH, sizeof(wifi_quick_scan_core_observation_t));
  if (s_queue == NULL) {
    ESP_LOGE(TAG, "Failed to allocate observation queue");
    goto cleanup;
  }
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to allocate state mutex");
    goto cleanup;
  }
  return ESP_OK;
cleanup:
  if (s_queue != NULL) {
    vQueueDelete(s_queue);
    s_queue = NULL;
  }
  free(s_core);
  s_core = NULL;
  return err;
}

esp_err_t wifi_quick_scan_start(const spi_wifi_quick_config_t *config) {
  spi_wifi_quick_config_t selected = {
      .duration_seconds = SPI_WIFI_QUICK_DEFAULT_SECONDS,
      .band = SPI_WIFI_BAND_DUAL,
  };
  if (config != NULL) {
    selected = *config;
    if (selected.duration_seconds == 0) {
      selected.duration_seconds = SPI_WIFI_QUICK_DEFAULT_SECONDS;
    }
  }
  if ((selected.duration_seconds != QUICK_DURATION_SHORT &&
       selected.duration_seconds != SPI_WIFI_QUICK_DEFAULT_SECONDS &&
       selected.duration_seconds != QUICK_DURATION_LONG) ||
      selected.band > SPI_WIFI_BAND_5G) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_status.state == SPI_WIFI_QUICK_RUNNING || wifi_service_is_busy() ||
      wifi_service_is_active()) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  s_config = selected;
  memset(s_core, 0, sizeof(*s_core));
  xQueueReset(s_queue);
  atomic_store(&s_cancel, false);
  atomic_store(&s_dropped, 0);
  s_start_us = esp_timer_get_time();
  s_status = (spi_wifi_quick_status_t){.state = SPI_WIFI_QUICK_RUNNING};
  BaseType_t result = xTaskCreatePinnedToCore(capture_task,
                                              "wifi_quick_scan",
                                              QUICK_STACK_SIZE,
                                              NULL,
                                              SYS_PRIO_SERVICE_HI,
                                              &s_task,
                                              SYS_CORE_MAIN);
  if (result != pdPASS) {
    s_task = NULL;
    s_status.state = SPI_WIFI_QUICK_ERROR;
    s_status.error = ESP_ERR_NO_MEM;
    xSemaphoreGive(s_mutex);
    ESP_LOGE(TAG, "Failed to create capture task");
    return ESP_ERR_NO_MEM;
  }
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

esp_err_t wifi_quick_scan_stop(void) {
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_status.state == SPI_WIFI_QUICK_RUNNING) {
    atomic_store(&s_cancel, true);
  }
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

esp_err_t wifi_quick_scan_get_status(spi_wifi_quick_status_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *out_status = s_status;
  if (s_status.state == SPI_WIFI_QUICK_RUNNING) {
    out_status->elapsed_ms = (uint32_t)((esp_timer_get_time() - s_start_us) / QUICK_US_PER_MS);
  }
  out_status->dropped_observations = atomic_load(&s_dropped);
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

esp_err_t wifi_quick_scan_get_record(uint16_t index,
                                     bool include_authorized,
                                     spi_wifi_quick_record_t *out_record) {
  if (out_record == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  esp_err_t err = ESP_ERR_NOT_FOUND;
  if (s_status.state == SPI_WIFI_QUICK_RUNNING || s_status.state == SPI_WIFI_QUICK_IDLE) {
    err = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  for (size_t i = 0; i < s_core->count; i++) {
    const spi_wifi_quick_record_t *record = &s_core->entries[i].record;
    if (!include_authorized && record->is_authorized) {
      continue;
    }
    if (index == 0) {
      *out_record = *record;
      err = ESP_OK;
      break;
    }
    index--;
  }
cleanup:
  xSemaphoreGive(s_mutex);
  return err;
}

esp_err_t wifi_quick_scan_set_authorized(const uint8_t mac[QUICK_MAC_LEN], bool is_authorized) {
  static const uint8_t ZERO_MAC[QUICK_MAC_LEN] = {0};
  if (mac == NULL || (mac[0] & QUICK_MAC_GROUP_BIT) != 0 ||
      memcmp(mac, ZERO_MAC, QUICK_MAC_LEN) == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  esp_err_t err = ESP_OK;
  if (s_status.state == SPI_WIFI_QUICK_RUNNING) {
    err = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  size_t index = 0;
  while (index < s_whitelist_count && memcmp(mac, s_whitelist[index], QUICK_MAC_LEN) != 0) {
    index++;
  }
  if (index < s_whitelist_count && !is_authorized) {
    s_whitelist_count--;
    memcpy(s_whitelist[index], s_whitelist[s_whitelist_count], QUICK_MAC_LEN);
  } else if (index == s_whitelist_count && is_authorized) {
    if (s_whitelist_count == WIFI_QUICK_SCAN_WHITELIST_MAX) {
      err = ESP_ERR_NO_MEM;
      goto cleanup;
    }
    memcpy(s_whitelist[s_whitelist_count++], mac, QUICK_MAC_LEN);
  }
cleanup:
  xSemaphoreGive(s_mutex);
  return err;
}

static void capture_task(void *ctx) {
  (void)ctx;
  // Start cannot change these fields until this task publishes its terminal state.
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  spi_wifi_quick_config_t config = s_config;
  xSemaphoreGive(s_mutex);
  wifi_service_capture_config_t capture = {
      .duration_ms = (uint32_t)config.duration_seconds * QUICK_MS_PER_SECOND,
      .band = config.band,
      .tick_cb = capture_tick,
  };
  ESP_LOGI(TAG, "Starting %u-second observation", config.duration_seconds);
  esp_err_t err = wifi_service_capture_passive(&capture, receive_frame);
  capture_tick(NULL);
  uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - s_start_us) / QUICK_US_PER_MS);
  wifi_quick_scan_core_finish(s_core, capture.duration_ms, s_whitelist, s_whitelist_count);
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status.elapsed_ms = elapsed_ms;
  s_status.error = (uint32_t)err;
  s_status.overflow_observations = s_core->overflow;
  s_status.total_count = s_core->count;
  s_status.unknown_count = 0;
  for (size_t i = 0; i < s_core->count; i++) {
    if (!s_core->entries[i].record.is_authorized) {
      s_status.unknown_count++;
    }
  }
  s_status.state = err != ESP_OK            ? SPI_WIFI_QUICK_ERROR
                   : atomic_load(&s_cancel) ? SPI_WIFI_QUICK_CANCELLED
                                            : SPI_WIFI_QUICK_DONE;
  s_task = NULL;
  xSemaphoreGive(s_mutex);
  ESP_LOGI(TAG, "Observation finished: %s", esp_err_to_name(err));
  vTaskDelete(NULL);
}

static void receive_frame(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (buf == NULL || type == WIFI_PKT_MISC) {
    return;
  }
  const wifi_promiscuous_pkt_t *packet = buf;
  if (packet->rx_ctrl.rx_state != 0 || packet->rx_ctrl.sig_len <= QUICK_FCS_LENGTH) {
    return;
  }
  wifi_quick_scan_core_observation_t observation;
  if (wifi_quick_scan_core_parse(
          packet->payload, packet->rx_ctrl.sig_len - QUICK_FCS_LENGTH, &observation) != ESP_OK) {
    return;
  }
  observation.channel = packet->rx_ctrl.channel;
  observation.rssi = packet->rx_ctrl.rssi;
  observation.elapsed_ms = (uint32_t)((esp_timer_get_time() - s_start_us) / QUICK_US_PER_MS);
  if (xQueueSend(s_queue, &observation, 0) != pdTRUE) {
    atomic_fetch_add(&s_dropped, 1);
  }
}

static bool capture_tick(void *ctx) {
  (void)ctx;
  wifi_quick_scan_core_observation_t observation;
  UBaseType_t count = uxQueueMessagesWaiting(s_queue);
  for (UBaseType_t i = 0; i < count; i++) {
    if (xQueueReceive(s_queue, &observation, 0) != pdTRUE) {
      break;
    }
    wifi_quick_scan_core_observe(s_core, &observation);
  }
  return !atomic_load(&s_cancel);
}
