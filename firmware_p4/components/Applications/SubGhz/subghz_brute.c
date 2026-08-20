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

#include "subghz_brute.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "cc1101.h"
#include "subghz_protocol_decoder.h"
#include "subghz_protocol_registry.h"
#include "subghz_receiver.h"
#include "subghz_transmitter.h"
#include "subghz_types.h"

static const char *TAG = "SUBGHZ_BRUTE";

#define BRUTE_MAX_CODES     65536
#define BRUTE_MAX_PULSES    2048
#define BRUTE_PACING_MS     100
#define BRUTE_RX_DRAIN_MS   150
#define BRUTE_TASK_STACK    4096
#define BRUTE_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define BRUTE_TASK_CORE     SYS_CORE_RADIO
#define BRUTE_LOCK_MS       20

static const subghz_protocol_t *s_proto = NULL;
static uint8_t s_bit_count = 0;
static volatile bool s_run = false;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static subghz_brute_status_t s_status;

static void set_status(uint32_t sent, uint32_t total, bool running, bool done) {
  if (s_mutex == NULL)
    return;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(BRUTE_LOCK_MS)) != pdTRUE)
    return;
  s_status.sent = sent;
  s_status.total = total;
  s_status.running = running;
  s_status.done = done;
  xSemaphoreGive(s_mutex);
}

static void brute_task(void *arg) {
  (void)arg;
  uint32_t total = s_status.total;
  int32_t *pulses = malloc(BRUTE_MAX_PULSES * sizeof(int32_t));

  if (pulses == NULL || s_proto == NULL || s_proto->encode == NULL) {
    if (pulses != NULL)
      free(pulses);
    set_status(0, total, false, true);
    s_run = false;
    s_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  for (uint32_t code = 0; code < total && s_run; code++) {
    subghz_data_t data = {0};
    data.protocol_name = s_proto->name;
    data.bit_count = s_bit_count;
    data.raw_value = code;
    data.serial = code;

    size_t n = s_proto->encode(&data, pulses, BRUTE_MAX_PULSES);
    if (n > 0)
      subghz_tx_send_raw(pulses, n);

    set_status(code + 1, total, true, false);
    vTaskDelay(pdMS_TO_TICKS(BRUTE_PACING_MS));
  }

  free(pulses);
  ESP_LOGI(TAG, "sweep complete (%lu codes)", (unsigned long)total);
  set_status(s_status.sent, total, false, true);
  s_run = false;
  s_task = NULL;
  vTaskDelete(NULL);
}

esp_err_t subghz_brute_start(const char *protocol, uint8_t bit_count, uint32_t freq) {
  if (protocol == NULL || bit_count == 0 || s_run)
    return ESP_ERR_INVALID_STATE;

  const subghz_protocol_t *p = subghz_protocol_registry_get_by_name(protocol);
  if (p == NULL || p->encode == NULL) {
    ESP_LOGW(TAG, "protocol '%s' has no encoder", protocol);
    return ESP_ERR_NOT_SUPPORTED;
  }

  uint32_t total = (bit_count >= 32) ? 0xFFFFFFFFu : (1u << bit_count);
  if (total == 0 || total > BRUTE_MAX_CODES)
    total = BRUTE_MAX_CODES;

  if (s_mutex == NULL)
    s_mutex = xSemaphoreCreateMutex();

  s_proto = p;
  s_bit_count = bit_count;
  set_status(0, total, true, false);

  if (subghz_receiver_is_running()) {
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(BRUTE_RX_DRAIN_MS));
  }

  esp_err_t err = subghz_tx_init();
  if (err != ESP_OK) {
    set_status(0, total, false, true);
    return err;
  }
  if (freq != 0)
    cc1101_set_frequency(freq);

  s_run = true;
  BaseType_t ret = xTaskCreatePinnedToCore(brute_task,
                                           "subghz_brute",
                                           BRUTE_TASK_STACK,
                                           NULL,
                                           BRUTE_TASK_PRIORITY,
                                           &s_task,
                                           BRUTE_TASK_CORE);
  if (ret != pdPASS) {
    s_run = false;
    set_status(0, total, false, true);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void subghz_brute_stop(void) {
  s_run = false;
}

bool subghz_brute_get_status(subghz_brute_status_t *out) {
  if (out == NULL || s_mutex == NULL)
    return false;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(BRUTE_LOCK_MS)) != pdTRUE)
    return false;
  *out = s_status;
  xSemaphoreGive(s_mutex);
  return true;
}
