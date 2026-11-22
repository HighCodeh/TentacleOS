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

#include "usb_power.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bq25896.h"
#include "husb320.h"
#include "led_control.h"
#include "sys_prio.h"

#define USB_POWER_POLL_HIGH_MS      20
#define USB_POWER_POLL_IDLE_MS      250
#define USB_POWER_STABLE_US         250000
#define USB_POWER_RETRY_US          2000000
#define USB_POWER_STALE_US          1000000
#define USB_POWER_REPORT_US         5000000
#define USB_POWER_MIN_ADVERTISED_MA 1500
#define USB_POWER_STACK_SIZE        3072
#define USB_POWER_LOCK_MS           20

static const char *TAG = "USB_POWER";
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static usb_power_snapshot_t s_snapshot;
static int64_t s_updated_us;
static int64_t s_stable_since_us;
static int64_t s_retry_at_us;
static bool s_has_candidate;
static bool s_has_snapshot;
static bool s_has_report_pending;
static int64_t s_report_at_us;

static uint32_t update_input(void);
static void worker(void *context);

esp_err_t usb_power_init(void) {
  if (s_task != NULL)
    return ESP_OK;
  uint16_t applied = 0;
  esp_err_t ret = bq25896_set_usb_input_limit(BQ25896_USB_FALLBACK_MA, &applied);
  if (ret != ESP_OK)
    return ret;
  if (s_lock == NULL)
    s_lock = xSemaphoreCreateMutex();
  if (s_lock == NULL) {
    ESP_LOGE(TAG, "Snapshot lock allocation failed");
    led_signal_error();
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreatePinnedToCore(worker,
                              "usb_power",
                              USB_POWER_STACK_SIZE,
                              NULL,
                              SYS_PRIO_SERVICE_HI,
                              &s_task,
                              SYS_CORE_RADIO) != pdPASS) {
    s_task = NULL;
    ESP_LOGE(TAG, "Worker allocation failed; USB input remains limited");
    led_signal_error();
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool usb_power_get(usb_power_snapshot_t *out_snapshot) {
  if (out_snapshot == NULL)
    return false;
  *out_snapshot = (usb_power_snapshot_t){0};
  if (s_lock == NULL || xSemaphoreTake(s_lock, 0) != pdTRUE)
    return false;
  bool is_fresh = s_has_snapshot && esp_timer_get_time() - s_updated_us <= USB_POWER_STALE_US;
  if (is_fresh)
    *out_snapshot = s_snapshot;
  xSemaphoreGive(s_lock);
  return is_fresh;
}

static uint32_t update_input(void) {
  husb320_source_t source = {0};
  esp_err_t source_result = husb320_read_source(&source);
  int64_t now = esp_timer_get_time();
  bool can_raise = source_result == ESP_OK && source.is_attached &&
                   source.advertised_ma >= USB_POWER_MIN_ADVERTISED_MA;
  if (!can_raise) {
    s_has_candidate = false;
  } else if (!s_has_candidate) {
    s_has_candidate = true;
    s_stable_since_us = now;
  }
  uint16_t requested = can_raise && now - s_stable_since_us >= USB_POWER_STABLE_US
                           ? BQ25896_USB_BOARD_MAX_MA
                           : BQ25896_USB_FALLBACK_MA;
  usb_power_snapshot_t next = {
      .is_source_valid = source_result == ESP_OK,
      .source_error = source_result,
      .source_reg = source.last_reg,
      .is_attached = source_result == ESP_OK && source.is_attached,
      .advertised_ma = source_result == ESP_OK ? source.advertised_ma : 0,
  };
  next.input_error = bq25896_set_usb_input_limit(requested, &next.applied_ma);
  if (next.input_error != ESP_OK) {
    s_has_candidate = false;
    // Retry at the fallback budget before permitting another increase.
    uint16_t fallback = 0;
    if (bq25896_set_usb_input_limit(BQ25896_USB_FALLBACK_MA, &fallback) == ESP_OK)
      next.applied_ma = fallback;
  }
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(USB_POWER_LOCK_MS)) == pdTRUE) {
    bool has_changed =
        !s_has_snapshot || next.applied_ma != s_snapshot.applied_ma ||
        next.is_source_valid != s_snapshot.is_source_valid ||
        next.source_error != s_snapshot.source_error || next.source_reg != s_snapshot.source_reg ||
        next.input_error != s_snapshot.input_error || next.is_attached != s_snapshot.is_attached;
    s_snapshot = next;
    s_updated_us = esp_timer_get_time();
    s_has_snapshot = true;
    xSemaphoreGive(s_lock);
    s_has_report_pending |= has_changed;
    if (s_has_report_pending && now >= s_report_at_us) {
      s_has_report_pending = false;
      s_report_at_us = now + USB_POWER_REPORT_US;
      if (next.input_error != ESP_OK || !next.is_source_valid) {
        ESP_LOGW(TAG,
                 "USB input %u mA; source=%s, last_reg=0x%02X, control=%s",
                 next.applied_ma,
                 esp_err_to_name(next.source_error),
                 next.source_reg,
                 esp_err_to_name(next.input_error));
        led_signal_warning();
      } else {
        ESP_LOGI(TAG,
                 "USB %s; CC=%u mA (0=default), input=%u mA",
                 next.is_attached ? "attached" : "detached",
                 next.advertised_ma,
                 next.applied_ma);
        led_signal_info();
      }
    }
  }
  // Restore sink configuration only after attempting the conservative input budget.
  if (source_result != ESP_OK && now >= s_retry_at_us) {
    (void)husb320_init();
    s_retry_at_us = esp_timer_get_time() + USB_POWER_RETRY_US;
  }
  return next.applied_ma > BQ25896_USB_FALLBACK_MA || next.input_error != ESP_OK
             ? USB_POWER_POLL_HIGH_MS
             : USB_POWER_POLL_IDLE_MS;
}

static void worker(void *context) {
  (void)context;
  for (;;) {
    uint32_t delay_ms = update_input();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}
