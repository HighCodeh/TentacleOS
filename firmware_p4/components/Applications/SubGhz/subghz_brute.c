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
#include <stdatomic.h>

#include "esp_heap_caps.h"
#include "led_control.h"
#include "subghz_settings.h"
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "cc1101.h"
#include "cc1101_rf_path.h"
#include "subghz_protocol_decoder.h"
#include "subghz_protocol_registry.h"
#include "subghz_receiver.h"
#include "subghz_transmitter.h"
#include "subghz_types.h"

static const char *TAG = "SUBGHZ_BRUTE";

#define BRUTE_MAX_BITS      64
#define BRUTE_MAX_CODES     65536
#define BRUTE_MAX_PULSES    2048
#define BRUTE_PACING_MS     100
#define BRUTE_RX_DRAIN_MS   1000
#define BRUTE_POLL_MS       10
#define BRUTE_TASK_STACK    4096
#define BRUTE_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define BRUTE_TASK_CORE     SYS_CORE_RADIO
#define BRUTE_LOCK_MS       20

static const subghz_protocol_t *s_proto = NULL;
static uint8_t s_bit_count = 0;
static atomic_bool s_run;
static atomic_bool s_active;
static subghz_tx_config_t s_config;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static subghz_brute_status_t s_status;

static void set_status(uint32_t sent, uint32_t total, bool running, bool done, esp_err_t error) {
  if (s_mutex == NULL)
    return;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(BRUTE_LOCK_MS)) != pdTRUE)
    return;
  s_status.sent = sent;
  s_status.total = total;
  s_status.running = running;
  s_status.done = done;
  s_status.error = error;
  xSemaphoreGive(s_mutex);
}

static void brute_task(void *arg) {
  (void)arg;
  uint32_t total = s_status.total;
  uint32_t sent = 0;
  esp_err_t error = ESP_OK;
  int32_t *pulses =
      heap_caps_malloc(BRUTE_MAX_PULSES * sizeof(int32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (pulses == NULL) {
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  for (uint32_t code = 0; code < total && atomic_load(&s_run); code++) {
    subghz_data_t data = {0};
    data.protocol_name = s_proto->name;
    data.bit_count = s_bit_count;
    data.raw_value = code;
    data.serial = code;
    size_t count = s_proto->encode(&data, pulses, BRUTE_MAX_PULSES);
    if (count == 0 || count > BRUTE_MAX_PULSES) {
      error = ESP_ERR_INVALID_SIZE;
      break;
    }
    error = subghz_tx_send_configured(pulses, count, &s_config);
    if (error != ESP_OK)
      break;
    subghz_tx_status_t status;
    do {
      subghz_tx_get_status(&status);
      if (!status.is_busy || !atomic_load(&s_run))
        break;
      vTaskDelay(pdMS_TO_TICKS(BRUTE_POLL_MS));
    } while (true);
    if (!atomic_load(&s_run))
      break;
    error = status.error;
    if (error != ESP_OK)
      break;
    sent++;
    set_status(sent, total, true, false, ESP_OK);
    for (uint32_t elapsed = 0; elapsed < BRUTE_PACING_MS && atomic_load(&s_run);
         elapsed += BRUTE_POLL_MS)
      vTaskDelay(pdMS_TO_TICKS(BRUTE_POLL_MS));
  }
cleanup:
  subghz_tx_stop();
  free(pulses);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Sweep stopped after %lu codes: %s", (unsigned long)sent, esp_err_to_name(error));
    led_signal_error();
  } else {
    ESP_LOGI(TAG,
             "Sweep %s (%lu/%lu codes)",
             sent == total ? "complete" : "cancelled",
             (unsigned long)sent,
             (unsigned long)total);
    led_signal_info();
  }
  set_status(sent, total, false, true, error);
  atomic_store(&s_run, false);
  s_task = NULL;
  atomic_store(&s_active, false);
  vTaskDelete(NULL);
}

esp_err_t subghz_brute_start(const char *protocol, uint8_t bit_count, uint32_t freq) {
  if (protocol == NULL || bit_count == 0 || bit_count > BRUTE_MAX_BITS || atomic_load(&s_active))
    return ESP_ERR_INVALID_STATE;

  if (cc1101_rf_path_for_frequency(freq) == CC1101_RF_PATH_NONE)
    return ESP_ERR_INVALID_ARG;

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

  if (s_mutex == NULL)
    return ESP_ERR_NO_MEM;
  bool expected = false;
  if (!atomic_compare_exchange_strong(&s_active, &expected, true))
    return ESP_ERR_INVALID_STATE;
  subghz_settings_t settings = subghz_settings_get();
  s_config = (subghz_tx_config_t){.frequency_hz = freq,
                                  .preset = settings.preset,
                                  .has_radio_config = true,
                                  .radio = settings.radio};
  s_proto = p;
  s_bit_count = bit_count;
  set_status(0, total, true, false, ESP_OK);

  if (subghz_receiver_is_running()) {
    subghz_receiver_stop();
    for (uint32_t elapsed = 0; elapsed < BRUTE_RX_DRAIN_MS && subghz_receiver_is_running();
         elapsed += BRUTE_POLL_MS)
      vTaskDelay(pdMS_TO_TICKS(BRUTE_POLL_MS));
  }

  esp_err_t err = subghz_receiver_is_running() ? ESP_ERR_TIMEOUT : subghz_tx_init();
  if (err != ESP_OK) {
    set_status(0, total, false, true, err);
    atomic_store(&s_active, false);
    return err;
  }

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
    subghz_tx_stop();
    err = ESP_ERR_NO_MEM;
    set_status(0, total, false, true, err);
    atomic_store(&s_active, false);
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
