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

#include "test_platform.h"
#include "resource_mgr.h"
#include "frequency_waterfall.c"
#include "frequency_waterfall_plan.c"
static bool has_grant, busy, fail_task, cancel_mid, pause_mid, pause_seen;
static unsigned calls, fail_at, samples, cleanup_failures;
static int64_t clock_us;
static void (*task)(void *);
static esp_err_t radio(void) { assert(has_grant); return ++calls == fail_at ? ESP_FAIL : ESP_OK; }
int64_t esp_timer_get_time(void) { return clock_us; }
void led_signal_error(void) {}
esp_err_t resource_acquire(const res_request_t *request, res_handle_t *out) {
  assert(request->id == RES_SUBGHZ && !request->allow_preempt);
  if (busy || has_grant) return ESP_ERR_INVALID_STATE;
  has_grant = true; *out = 42; return ESP_OK;
}
esp_err_t resource_release(res_handle_t handle) { assert(handle == 42 && has_grant); has_grant = false; return ESP_OK; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack,
  void *arg, int priority, TaskHandle_t *handle, int core) {
  assert(core == SYS_CORE_RADIO && priority == SYS_PRIO_BACKGROUND_LO);
  task = fn; return fail_task ? pdFALSE : pdPASS;
}
void vTaskDelete(TaskHandle_t handle) { assert(!has_grant); }
esp_err_t cc1101_init(void) { return radio(); }
esp_err_t cc1101_set_preset(cc1101_preset_t preset, uint32_t frequency) {
  if (preset == CC1101_PRESET_IDLE && cleanup_failures) { --cleanup_failures; return ESP_FAIL; }
  return radio();
}
esp_err_t cc1101_strobe(uint8_t command) { return radio(); }
esp_err_t cc1101_write_reg(uint8_t reg, uint8_t value) { return radio(); }
esp_err_t cc1101_set_rx_bandwidth(float bandwidth) { assert(bandwidth > 0); return radio(); }
esp_err_t cc1101_scan_rssi(uint32_t frequency, float *out) {
  assert(frequency >= 432920000 && frequency <= 434920000);
  ++samples; clock_us += 2000;
  if (cancel_mid && samples == 17) frequency_waterfall_stop();
  if (pause_mid && samples == 17) frequency_waterfall_pause(true);
  *out = -40 - samples % 60;
  return radio();
}
void vTaskDelay(TickType_t ticks) {
  assert(has_grant); clock_us += ticks * 1000;
  if (s_snapshot.is_paused) {
    assert(s_snapshot.sequence == 0); pause_seen = true;
    frequency_waterfall_pause(false);
  }
  if (s_snapshot.sequence) frequency_waterfall_stop();
}
static void reset(void) {
  assert(!frequency_waterfall_is_running() && !has_grant);
  busy = fail_task = cancel_mid = pause_mid = pause_seen = false;
  calls = fail_at = samples = cleanup_failures = 0; clock_us = 0;
}
int main(void) {
  frequency_waterfall_config_t config = {433920000, 2000000, 3};
  assert(frequency_waterfall_validate(&config) == ESP_OK);
  assert(frequency_waterfall_bin_hz(&config, 0) == 432920000);
  assert(frequency_waterfall_bin_hz(&config, FREQUENCY_WATERFALL_BINS - 1) == 434920000);
  for (unsigned i = 1; i < FREQUENCY_WATERFALL_BINS; ++i) assert(frequency_waterfall_bin_hz(&config,i) > frequency_waterfall_bin_hz(&config,i-1));
  frequency_waterfall_config_t bad = {350000000, 20000000, 3};
  assert(frequency_waterfall_validate(&bad) == ESP_ERR_INVALID_ARG);
  bad = config; bad.center_hz = UINT32_MAX;
  assert(frequency_waterfall_validate(&bad) == ESP_ERR_INVALID_ARG);
  bad = config; bad.bandwidth = 255; assert(frequency_waterfall_validate(&bad) == ESP_ERR_INVALID_ARG);
  bad = config; bad.span_hz = 0; assert(frequency_waterfall_validate(&bad) == ESP_ERR_INVALID_ARG);
  bad = config; assert(frequency_waterfall_fit_center(&bad, 300000000) == ESP_OK);
  assert(frequency_waterfall_bin_hz(&bad, 0) == 300000000);
  assert(frequency_waterfall_fit_center(&bad, 928000000) == ESP_OK);
  assert(frequency_waterfall_bin_hz(&bad, FREQUENCY_WATERFALL_BINS - 1) == 928000000);
  assert(frequency_waterfall_fit_center(&bad, 500000000) == ESP_ERR_INVALID_ARG);
  reset(); fail_task = true; assert(frequency_waterfall_start(&config) == ESP_ERR_NO_MEM && !has_grant);
  reset(); busy = true; assert(frequency_waterfall_start(&config) == ESP_ERR_INVALID_STATE && !has_grant);
  reset(); assert(frequency_waterfall_start(&config) == ESP_OK); task(NULL);
  assert(s_snapshot.sequence == 1 && samples == FREQUENCY_WATERFALL_BINS && s_snapshot.duration_us == FREQUENCY_WATERFALL_BINS * 2000);
  unsigned total = calls;
  for (unsigned i = 1; i <= total; ++i) {
    reset(); fail_at = i; assert(frequency_waterfall_start(&config) == ESP_OK); task(NULL);
    assert(!has_grant && !frequency_waterfall_is_running() && s_snapshot.error == ESP_FAIL);
  }
  reset(); cancel_mid = true; assert(frequency_waterfall_start(&config) == ESP_OK); task(NULL);
  assert(s_snapshot.sequence == 0 && samples == 17);
  reset(); pause_mid = true; assert(frequency_waterfall_start(&config) == ESP_OK); task(NULL);
  assert(pause_seen && s_snapshot.sequence == 1 && samples == 17 + FREQUENCY_WATERFALL_BINS);
  reset(); cleanup_failures = 3; assert(frequency_waterfall_start(&config) == ESP_OK); task(NULL);
  assert(!has_grant && cleanup_failures == 0 && s_snapshot.error == ESP_FAIL);
  reset(); assert(frequency_waterfall_start(&config) == ESP_OK); frequency_waterfall_stop(); task(NULL);
  assert(!calls && !has_grant);
  puts("PASS: waterfall grid/band validation, complete rows, pause discards partial rows, cancellation, startup and injected radio/cleanup failures");
}
