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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wifi_quick_scan.h"
#include "wifi_quick_scan_core.h"
#include "wifi_service.h"

static uint8_t queue_bytes[64 * sizeof(wifi_quick_scan_core_observation_t)];
static unsigned queue_count, queue_head, queue_tail;
static void (*worker)(void *);
static int64_t now_us;
static bool active, fail_task, cancel_capture, flood_capture;
static esp_err_t capture_error;
static unsigned deleted_tasks;
static uint8_t mac[6] = {0x24, 0x6F, 0x28, 1, 2, 3};

void *heap_caps_calloc(size_t count, size_t size, unsigned caps) {
  (void)caps; return calloc(count, size);
}
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
int64_t esp_timer_get_time(void) { return now_us; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t timeout) {
  (void)mutex; (void)timeout; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) { (void)mutex; return pdTRUE; }
QueueHandle_t xQueueCreate(unsigned count, size_t size) {
  assert(count * size == sizeof(queue_bytes)); return queue_bytes;
}
void vQueueDelete(QueueHandle_t queue) { (void)queue; }
BaseType_t xQueueReset(QueueHandle_t queue) {
  (void)queue; queue_count = queue_head = queue_tail = 0; return pdTRUE;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *data, uint32_t timeout) {
  (void)queue; (void)timeout;
  if (queue_count == 64) return 0;
  memcpy(queue_bytes + queue_tail * sizeof(wifi_quick_scan_core_observation_t), data,
         sizeof(wifi_quick_scan_core_observation_t));
  queue_tail = (queue_tail + 1) % 64; queue_count++; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *out, uint32_t timeout) {
  (void)queue; (void)timeout;
  if (!queue_count) return 0;
  memcpy(out, queue_bytes + queue_head * sizeof(wifi_quick_scan_core_observation_t),
         sizeof(wifi_quick_scan_core_observation_t));
  queue_head = (queue_head + 1) % 64; queue_count--; return pdTRUE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) { (void)queue; return queue_count; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack,
                                  void *ctx, unsigned priority, TaskHandle_t *out, unsigned core) {
  (void)name; (void)ctx;
  assert(stack == 4096 && priority == 5 && core == 0);
  if (fail_task) return 0;
  worker = fn; *out = (void *)1; return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { assert(task == NULL); deleted_tasks++; }
bool wifi_service_is_busy(void) { return false; }
bool wifi_service_is_active(void) { return active; }
esp_err_t wifi_service_capture_passive(const wifi_service_capture_config_t *config,
                                      wifi_promiscuous_cb_t receive) {
  assert(config->duration_ms == 45000 && config->band == SPI_WIFI_BAND_DUAL);
  if (capture_error != ESP_OK) return capture_error;
  wifi_promiscuous_pkt_t packet = {.rx_ctrl = {.rssi = -40, .channel = 1, .sig_len = 28}};
  packet.payload[0] = 0x40;
  memcpy(packet.payload + 10, mac, 6);
  for (unsigned i = 0; i < 3; i++) {
    now_us = i * 12500000LL;
    receive(&packet, WIFI_PKT_MGMT);
    assert(config->tick_cb(config->ctx));
  }
  if (flood_capture) {
    for (unsigned i = 0; i < 100; i++) receive(&packet, WIFI_PKT_MGMT);
    config->tick_cb(config->ctx);
  }
  if (cancel_capture) {
    assert(wifi_quick_scan_stop() == ESP_OK);
    assert(!config->tick_cb(config->ctx));
  }
  return ESP_OK;
}

static spi_wifi_quick_status_t status(void) {
  spi_wifi_quick_status_t result;
  assert(wifi_quick_scan_get_status(&result) == ESP_OK);
  return result;
}

int main(void) {
  spi_wifi_quick_record_t record;
  assert(wifi_quick_scan_start(NULL) == ESP_ERR_INVALID_STATE);
  assert(wifi_quick_scan_init() == ESP_OK);
  assert(wifi_quick_scan_init() == ESP_OK);
  assert(status().state == SPI_WIFI_QUICK_IDLE);
  assert(wifi_quick_scan_get_record(0, false, &record) == ESP_ERR_INVALID_STATE);
  spi_wifi_quick_config_t config = {.duration_seconds = 31};
  assert(wifi_quick_scan_start(&config) == ESP_ERR_INVALID_ARG);
  config.duration_seconds = 45; config.band = 3;
  assert(wifi_quick_scan_start(&config) == ESP_ERR_INVALID_ARG);
  active = true;
  assert(wifi_quick_scan_start(NULL) == ESP_ERR_INVALID_STATE);
  active = false;
  assert(wifi_quick_scan_start(NULL) == ESP_OK);
  assert(status().state == SPI_WIFI_QUICK_RUNNING);
  assert(wifi_quick_scan_start(NULL) == ESP_ERR_INVALID_STATE);
  assert(wifi_quick_scan_set_authorized(mac, true) == ESP_ERR_INVALID_STATE);
  assert(wifi_quick_scan_get_record(0, false, &record) == ESP_ERR_INVALID_STATE);
  worker(NULL);
  assert(status().state == SPI_WIFI_QUICK_DONE && status().total_count == 1);
  assert(wifi_quick_scan_get_record(0, false, &record) == ESP_OK);
  assert(record.score == 75 && record.should_investigate);
  assert(wifi_quick_scan_get_record(1, false, &record) == ESP_ERR_NOT_FOUND);
  assert(wifi_quick_scan_set_authorized(mac, true) == ESP_OK);
  now_us = 0;
  assert(wifi_quick_scan_start(NULL) == ESP_OK);
  worker(NULL);
  assert(status().total_count == 1 && status().unknown_count == 0);
  assert(wifi_quick_scan_get_record(0, false, &record) == ESP_ERR_NOT_FOUND);
  assert(wifi_quick_scan_get_record(0, true, &record) == ESP_OK && record.is_authorized);
  assert(wifi_quick_scan_set_authorized(mac, false) == ESP_OK);
  cancel_capture = true; now_us = 0;
  assert(wifi_quick_scan_start(NULL) == ESP_OK);
  worker(NULL);
  assert(status().state == SPI_WIFI_QUICK_CANCELLED && status().total_count == 1);
  cancel_capture = false; flood_capture = true; now_us = 0;
  assert(wifi_quick_scan_start(NULL) == ESP_OK);
  worker(NULL);
  assert(status().dropped_observations == 36);
  flood_capture = false; capture_error = ESP_FAIL;
  assert(wifi_quick_scan_start(NULL) == ESP_OK);
  worker(NULL);
  assert(status().state == SPI_WIFI_QUICK_ERROR && status().total_count == 0);
  assert(status().error == (uint32_t)ESP_FAIL && status().dropped_observations == 0);
  fail_task = true;
  assert(wifi_quick_scan_start(NULL) == ESP_ERR_NO_MEM);
  assert(status().state == SPI_WIFI_QUICK_ERROR && status().error == ESP_ERR_NO_MEM);
  assert(deleted_tasks == 5);
  return 0;
}
