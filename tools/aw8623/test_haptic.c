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
#include <stdio.h>
#include "haptic.c"

static void (*s_pending)(void *);
static bool s_allocation_failure, s_isr, s_cancel_during_play;
static int s_plays, s_probes, s_stops, s_stop_failures;
static esp_err_t s_play_result;
static const board_control_ops_t s_controls = {0};
const board_control_ops_t *board_service_controls(void) { return &s_controls; }
int xPortInIsrContext(void) { return s_isr; }
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, int stack, void *arg,
                           int priority, void *handle, int core) {
  (void)name; (void)stack; (void)arg; (void)priority; (void)handle; (void)core;
  if (s_allocation_failure) return 0;
  assert(s_pending == NULL);
  s_pending = fn;
  return pdPASS;
}
void vTaskDelete(void *task) { assert(task == NULL); }
void vTaskDelay(int ticks) {
  assert(ticks == HAPTIC_CLEANUP_MS);
  assert(haptic_is_busy());
  assert(haptic_play(70, 80) == ESP_ERR_INVALID_STATE);
}
esp_err_t aw8623_play(const board_control_ops_t *controls, const aw8623_playback_t *request) {
  assert(controls == &s_controls);
  assert(haptic_is_busy());
  assert(request->intensity == 70 && request->duration_ms == 80);
  assert(!request->is_cancelled());
  s_plays++;
  if (s_cancel_during_play) {
    haptic_stop();
    assert(request->is_cancelled());
    return ESP_ERR_INVALID_STATE;
  }
  return s_play_result;
}
esp_err_t aw8623_probe(const board_control_ops_t *controls, aw8623_diagnostic_t *result) {
  assert(controls == &s_controls && result != NULL);
  s_probes++;
  return ESP_OK;
}
esp_err_t aw8623_stop(const board_control_ops_t *controls) {
  assert(controls == &s_controls && haptic_is_busy());
  s_stops++;
  if (s_stop_failures > 0) { s_stop_failures--; return ESP_ERR_TIMEOUT; }
  return ESP_OK;
}
static void run_worker(void) {
  assert(s_pending != NULL);
  void (*fn)(void *) = s_pending;
  s_pending = NULL;
  fn(NULL);
  assert(!haptic_is_busy());
}
int main(void) {
  assert(haptic_play(0, 80) == ESP_ERR_INVALID_ARG);
  assert(haptic_play(101, 80) == ESP_ERR_INVALID_ARG);
  assert(haptic_play(70, 501) == ESP_ERR_INVALID_ARG);
  s_isr = true;
  assert(haptic_play(70, 80) == ESP_ERR_INVALID_STATE);
  s_isr = false;
  s_allocation_failure = true;
  assert(haptic_play(70, 80) == ESP_ERR_NO_MEM);
  assert(!haptic_is_busy() && haptic_last_result() == ESP_ERR_NO_MEM);
  s_allocation_failure = false;
  assert(haptic_play(70, 80) == ESP_OK);
  assert(haptic_probe() == ESP_ERR_INVALID_STATE);
  haptic_stop();
  run_worker();
  assert(s_plays == 0 && s_stops == 1);
  assert(haptic_play(70, 80) == ESP_OK);
  s_stop_failures = 3;
  run_worker();
  assert(s_plays == 1 && s_stops == 5 && haptic_last_result() == ESP_OK);
  s_cancel_during_play = true;
  assert(haptic_play(70, 80) == ESP_OK);
  run_worker();
  assert(s_plays == 2 && haptic_last_result() == ESP_ERR_INVALID_STATE);
  s_cancel_during_play = false;
  s_play_result = ESP_ERR_TIMEOUT;
  assert(haptic_play(70, 80) == ESP_OK);
  run_worker();
  assert(s_plays == 3 && haptic_last_result() == ESP_ERR_TIMEOUT);
  assert(haptic_probe() == ESP_OK);
  run_worker();
  assert(s_probes == 1 && s_plays == 3 && haptic_last_result() == ESP_OK);
  puts("PASS: haptic service ownership, cancellation, stop retry, no replay, allocation failure and ISR rejection");
  return 0;
}
