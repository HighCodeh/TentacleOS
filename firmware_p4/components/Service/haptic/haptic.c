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

#include "haptic.h"

#include <stdatomic.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "aw8623.h"
#include "board_service.h"
#include "sys_prio.h"

#define HAPTIC_STACK_BYTES 4096
#define HAPTIC_CLEANUP_MS  100

static const char *TAG = "HAPTIC";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_busy;
static atomic_bool s_cancelled;
static atomic_int s_result = ESP_ERR_NOT_FINISHED;
static aw8623_playback_t s_request;
static bool s_probe;

static bool cancelled(void) {
  return atomic_load(&s_cancelled);
}
static void worker(void *arg) {
  (void)arg;
  esp_err_t result = ESP_ERR_INVALID_STATE;
  if (!cancelled()) {
    if (s_probe) {
      aw8623_diagnostic_t diagnostic;
      result = aw8623_probe(board_service_controls(), &diagnostic);
    } else
      result = aw8623_play(board_service_controls(), &s_request);
  }
  atomic_store(&s_result, result);
  // Retain ownership while stop is unconfirmed; never replay on reconnect.
  while (aw8623_stop(board_service_controls()) != ESP_OK)
    vTaskDelay(pdMS_TO_TICKS(HAPTIC_CLEANUP_MS));
  portENTER_CRITICAL(&s_lock);
  s_busy = false;
  portEXIT_CRITICAL(&s_lock);
  vTaskDelete(NULL);
}

static esp_err_t submit(uint8_t intensity, uint16_t duration_ms, bool probe) {
  if (xPortInIsrContext())
    return ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&s_lock);
  if (s_busy) {
    portEXIT_CRITICAL(&s_lock);
    return ESP_ERR_INVALID_STATE;
  }
  s_busy = true;
  s_probe = probe;
  s_request = (aw8623_playback_t){
      .intensity = intensity, .duration_ms = duration_ms, .is_cancelled = cancelled};
  atomic_store(&s_cancelled, false);
  atomic_store(&s_result, ESP_ERR_NOT_FINISHED);
  portEXIT_CRITICAL(&s_lock);
  if (xTaskCreatePinnedToCore(
          worker, "haptic", HAPTIC_STACK_BYTES, NULL, SYS_PRIO_SERVICE_LO, NULL, SYS_CORE_RADIO) ==
      pdPASS)
    return ESP_OK;
  atomic_store(&s_result, ESP_ERR_NO_MEM);
  portENTER_CRITICAL(&s_lock);
  s_busy = false;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGE(TAG, "Worker allocation failed");
  return ESP_ERR_NO_MEM;
}

esp_err_t haptic_play(uint8_t intensity, uint16_t duration_ms) {
  if (intensity == 0 || intensity > AW8623_INTENSITY_MAX || duration_ms == 0 ||
      duration_ms > AW8623_MAX_DURATION_MS)
    return ESP_ERR_INVALID_ARG;
  return submit(intensity, duration_ms, false);
}
esp_err_t haptic_probe(void) {
  return submit(0, 0, true);
}
void haptic_stop(void) {
  if (xPortInIsrContext())
    return;
  portENTER_CRITICAL(&s_lock);
  atomic_store(&s_cancelled, true);
  portEXIT_CRITICAL(&s_lock);
}
bool haptic_is_busy(void) {
  portENTER_CRITICAL(&s_lock);
  bool busy = s_busy;
  portEXIT_CRITICAL(&s_lock);
  return busy;
}
esp_err_t haptic_last_result(void) {
  return atomic_load(&s_result);
}
