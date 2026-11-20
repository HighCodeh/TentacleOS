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

#include "board_control.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BOARD_CONFIRM_US      1200000
#define BOARD_CONFIRM_POLL_MS 10

static const char *TAG = "BOARD_CONTROL";

esp_err_t board_control_apply(const board_control_ops_t *ops,
                              board_control_id_t control,
                              bool value,
                              uint64_t *out_epoch) {
  if (xPortInIsrContext())
    return ESP_ERR_INVALID_STATE;
  if (ops == NULL || ops->get_status == NULL)
    return ESP_ERR_NOT_SUPPORTED;
  if (control >= BOARD_CONTROL_COUNT || control < 0)
    return ESP_ERR_INVALID_ARG;
  board_control_status_t state;
  esp_err_t ret = ops->get_status(&state);
  if (ret != ESP_OK)
    return ret;
  if (!state.is_compatible)
    return state.link_result == ESP_OK ? ESP_ERR_INVALID_STATE : state.link_result;
  if (!(state.capabilities & (1U << control)))
    return ESP_ERR_NOT_SUPPORTED;
  if (state.outputs[control].is_pending)
    return ESP_ERR_INVALID_STATE;
  uint64_t epoch = state.epoch;
  if (state.outputs[control].is_known && state.outputs[control].confirmed == value) {
    if (out_epoch != NULL)
      *out_epoch = epoch;
    return ESP_OK;
  }
  esp_err_t (*submit)(bool) = control == BOARD_CONTROL_AMP   ? ops->request_amplifier
                              : control == BOARD_CONTROL_USB ? ops->request_usb_enable
                                                             : ops->request_haptic_release;
  if (submit == NULL)
    return ESP_ERR_NOT_SUPPORTED;
  ret = submit(value);
  if (ret != ESP_OK)
    return ret;
  int64_t deadline = esp_timer_get_time() + BOARD_CONFIRM_US;
  do {
    ret = ops->get_status(&state);
    if (ret != ESP_OK)
      return ret;
    if (!state.is_compatible || state.epoch != epoch)
      return ESP_ERR_INVALID_STATE;
    if (!state.outputs[control].is_pending) {
      if (state.outputs[control].result != ESP_OK)
        return state.outputs[control].result;
      if (!state.outputs[control].is_known || state.outputs[control].confirmed != value)
        return ESP_ERR_INVALID_RESPONSE;
      if (out_epoch != NULL)
        *out_epoch = epoch;
      return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(BOARD_CONFIRM_POLL_MS));
  } while (esp_timer_get_time() < deadline);
  ESP_LOGW(TAG, "Control confirmation timed out");
  return ESP_ERR_TIMEOUT;
}
