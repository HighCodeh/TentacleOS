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

#include "tusb_desc.h"

#include <stdatomic.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "pin_def.h"
#include "sys_prio.h"

#define MUX_POLL_MS       100
#define MUX_DISCONNECT_MS 100
#define MUX_STACK_SIZE    4096

static const char *TAG = "USB_MUX";
static const board_control_ops_t *s_controls;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_worker;
static atomic_bool s_native;
static atomic_bool s_safe_requested;
static atomic_int s_result = ESP_ERR_INVALID_STATE;
static uint64_t s_epoch;
static portMUX_TYPE s_epoch_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_pending, s_requested, s_restore;

static esp_err_t switch_route(bool native);
static void worker(void *arg);

void usb_mux_bind_controls(const board_control_ops_t *ops) {
  s_controls = ops;
}

void usb_mux_init(void) {
  if (s_mutex != NULL)
    return;
  // board_pins_init already owns GPIO19 and selected CP2105 before any service.
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Mutex allocation failed");
    return;
  }
  s_restore = true;
  if (xTaskCreatePinnedToCore(worker,
                              "usb_mux",
                              MUX_STACK_SIZE,
                              NULL,
                              SYS_PRIO_SERVICE_LO,
                              &s_worker,
                              SYS_CORE_RADIO) != pdPASS) {
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    ESP_LOGE(TAG, "Worker allocation failed");
  }
}

esp_err_t usb_mux_request_native(bool native) {
  if (xPortInIsrContext() || s_mutex == NULL)
    return ESP_ERR_INVALID_STATE;
  if (!native) {
    atomic_store(&s_safe_requested, true);
    atomic_store(&s_result, ESP_ERR_NOT_FINISHED);
    xTaskNotifyGive(s_worker);
    return ESP_OK;
  }
  if (xSemaphoreTake(s_mutex, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  esp_err_t ret = ESP_OK;
  if (s_pending)
    ret = ESP_ERR_INVALID_STATE;
  else {
    s_pending = true;
    s_requested = native;
    atomic_store(&s_result, ESP_ERR_NOT_FINISHED);
  }
  xSemaphoreGive(s_mutex);
  if (ret == ESP_OK)
    xTaskNotifyGive(s_worker);
  return ret;
}

// Blocking worker API: serializes the entire disconnect/select/reconnect sequence.
esp_err_t usb_mux_set_native(bool native) {
  if (xPortInIsrContext() || s_mutex == NULL)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(s_mutex, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  if (s_pending) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t ret = switch_route(native);
  atomic_store(&s_result, ret);
  xSemaphoreGive(s_mutex);
  return ret;
}

esp_err_t usb_mux_result(void) {
  return atomic_load(&s_safe_requested) ? ESP_ERR_NOT_FINISHED : atomic_load(&s_result);
}

bool usb_mux_is_native(void) {
  board_control_status_t state;
  portENTER_CRITICAL(&s_epoch_lock);
  uint64_t epoch = s_epoch;
  portEXIT_CRITICAL(&s_epoch_lock);
  return atomic_load(&s_native) && s_controls != NULL && s_controls->get_status(&state) == ESP_OK &&
         state.is_compatible && state.epoch == epoch && state.outputs[BOARD_CONTROL_USB].is_known &&
         state.outputs[BOARD_CONTROL_USB].confirmed;
}

static esp_err_t switch_route(bool native) {
  uint64_t epoch = 0;
  esp_err_t ret = native ? busb_init() : ESP_OK;
  if (ret != ESP_OK)
    return ret;
  ret = board_control_apply(s_controls, BOARD_CONTROL_USB, false, &epoch);
  if (ret == ESP_OK) {
    atomic_store(&s_native, false);
    vTaskDelay(pdMS_TO_TICKS(MUX_DISCONNECT_MS));
    // An epoch change during detach must not reconnect an unconfirmed route.
    board_control_status_t state;
    ret = s_controls->get_status(&state);
    if (ret == ESP_OK && (!state.is_compatible || state.epoch != epoch))
      ret = ESP_ERR_INVALID_STATE;
    if (ret == ESP_OK)
      ret = gpio_set_level(GPIO_USB_MUX_SEL_PIN, native ? 1 : 0);
    uint64_t enabled_epoch = 0;
    if (ret == ESP_OK)
      ret = board_control_apply(s_controls, BOARD_CONTROL_USB, true, &enabled_epoch);
    if (ret == ESP_OK && enabled_epoch != epoch)
      ret = ESP_ERR_INVALID_STATE;
  }
  if (ret == ESP_OK) {
    portENTER_CRITICAL(&s_epoch_lock);
    s_epoch = epoch;
    portEXIT_CRITICAL(&s_epoch_lock);
    atomic_store(&s_native, native);
    s_restore = false;
    ESP_LOGI(TAG, "Route confirmed: %s", native ? "P4 native" : "CP2105");
  } else {
    // Select UART even when C5 is absent; its boot/watchdog also enables /OE.
    esp_err_t rollback = gpio_set_level(GPIO_USB_MUX_SEL_PIN, 0);
    atomic_store(&s_native, false);
    s_restore = true;
    if (rollback != ESP_OK)
      ESP_LOGE(TAG, "UART select failed: %s", esp_err_to_name(rollback));
    ESP_LOGW(TAG, "Switch failed: %s; restoring CP2105", esp_err_to_name(ret));
  }
  return ret;
}

static void poll_once(void) {
  if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {
    if (atomic_exchange(&s_safe_requested, false)) {
      s_pending = false;
      atomic_store(&s_result, ESP_ERR_NOT_FINISHED);
      atomic_store(&s_result, switch_route(false));
    } else if (s_pending) {
      atomic_store(&s_result, switch_route(s_requested));
      s_pending = false;
    }
    if (atomic_load(&s_native) && !usb_mux_is_native()) {
      atomic_store(&s_native, false);
      atomic_store(&s_result, ESP_ERR_INVALID_STATE);
      s_restore = true;
    }
    if (s_restore && gpio_set_level(GPIO_USB_MUX_SEL_PIN, 0) == ESP_OK) {
      if (board_control_apply(s_controls, BOARD_CONTROL_USB, true, NULL) == ESP_OK)
        s_restore = false;
    }
    xSemaphoreGive(s_mutex);
  }
}

static void worker(void *arg) {
  (void)arg;
  for (;;) {
    poll_once();
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MUX_POLL_MS));
  }
}
