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
#include "frequency_analyzer.c"
#include "frequency_analyzer_plan.c"

static bool has_grant, fail_task, busy, paused_once, pause_seen, reconfigure, reconfigured;
static bool flat_noise, no_signal, disappear;
static uint32_t tuned, target;
static float bandwidth;
static unsigned calls, fail_at, idle_failures, idle_calls, delays, stop_after, rx_calls;
static int64_t clock_us;
int64_t esp_timer_get_time(void) { return clock_us; }
static void (*task)(void *);
static esp_err_t radio(void) { assert(has_grant); return ++calls == fail_at ? ESP_FAIL : ESP_OK; }
esp_err_t resource_acquire(const res_request_t *request, res_handle_t *out) {
  assert(request->id == RES_SUBGHZ && !request->allow_preempt);
  if (has_grant || busy) return ESP_ERR_INVALID_STATE;
  has_grant = true; *out = 42; return ESP_OK;
}
esp_err_t resource_release(res_handle_t grant) { assert(has_grant && grant == 42); has_grant = false; return ESP_OK; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack,
                                 void *arg, int priority, TaskHandle_t *handle, int core) {
  assert(priority == SYS_PRIO_BACKGROUND_LO && core == SYS_CORE_RADIO && has_grant);
  if (fail_task) return pdFALSE;
  task = fn; return pdPASS;
}
void vTaskDelete(TaskHandle_t handle) { assert(!has_grant); }
void led_signal_error(void) {}
esp_err_t cc1101_init(void) { return radio(); }
esp_err_t cc1101_set_frequency(uint32_t frequency) {
  assert(cc1101_rf_path_for_frequency(frequency) != CC1101_RF_PATH_NONE);
  tuned = frequency; return radio();
}
esp_err_t cc1101_set_preset(cc1101_preset_t preset, uint32_t frequency) {
  if (preset == CC1101_PRESET_IDLE) {
    idle_calls++;
    if (idle_failures) { idle_failures--; return ESP_ERR_TIMEOUT; }
  } else assert(preset == CC1101_PRESET_OOK_650KHZ);
  return radio();
}
esp_err_t cc1101_strobe(uint8_t command) { assert(command == CC1101_SIDLE); return radio(); }
esp_err_t cc1101_write_reg(uint8_t reg, uint8_t value) { return radio(); }
esp_err_t cc1101_set_rx_bandwidth(float khz) { bandwidth = khz; return radio(); }
esp_err_t cc1101_enter_rx_mode(void) { rx_calls++; return radio(); }
esp_err_t cc1101_get_register(uint8_t reg, uint8_t *out) {
  assert(reg == (CC1101_RSSI | 0x40));
  esp_err_t error = radio();
  float dbm = -110;
  if (!no_signal) {
    float delta = fabsf((float)((int64_t)tuned - target));
    if (bandwidth > 600 && delta < 325000) dbm = -45;
    if (bandwidth < 60 && !disappear) dbm = fmaxf(-110, -45 - delta / 2500);
  }
  if (flat_noise) dbm = -45;
  *out = (uint8_t)(int16_t)((dbm + 74) * 2);
  return error;
}
float cc1101_convert_rssi(uint8_t raw) { return (raw >= 128 ? raw - 256 : raw) / 2.0f - 74; }
esp_err_t cc1101_scan_rssi(uint32_t frequency, float *out) {
  esp_err_t error = cc1101_set_frequency(frequency);
  if (error == ESP_OK) error = cc1101_enter_rx_mode();
  if (error != ESP_OK) return error;
  vTaskDelay(2);
  uint8_t raw;
  error = cc1101_get_register(CC1101_RSSI | 0x40, &raw);
  if (error == ESP_OK) *out = cc1101_convert_rssi(raw);
  return error;
}
void vTaskDelay(TickType_t ticks) {
  clock_us += ticks * 1000;
  assert(++delays < 50000 && has_grant);
  if (s_state == WORKER_STOPPING) return;
  if (reconfigure && !reconfigured && s_snapshot.phase == FREQUENCY_ANALYZER_REFINING) {
    reconfigured = true;
    assert(frequency_analyzer_configure(FREQUENCY_ANALYZER_915, -70) == ESP_OK);
    frequency_analyzer_stop();
  }
  if (stop_after && delays >= stop_after) frequency_analyzer_stop();
  if (paused_once && !pause_seen && s_snapshot.detections)
    frequency_analyzer_pause(true);
  if (s_snapshot.phase == FREQUENCY_ANALYZER_PAUSED) {
    pause_seen = true; assert(idle_calls > 0);
    frequency_analyzer_pause(false); frequency_analyzer_stop();
  }
  if (s_snapshot.sweeps >= 1 && !paused_once) frequency_analyzer_stop();
}
static void reset(void) {
  assert(!has_grant && !frequency_analyzer_is_running());
  fail_task = busy = paused_once = pause_seen = reconfigure = reconfigured = false;
  flat_noise = no_signal = disappear = false;
  target = 315000000; bandwidth = 650; task = NULL;
  calls = fail_at = idle_failures = idle_calls = delays = stop_after = rx_calls = 0;
  clock_us = 0;
}
static void run(frequency_analyzer_mode_t mode) {
  assert(frequency_analyzer_start(mode, -70) == ESP_OK && task != NULL && has_grant);
  assert(frequency_analyzer_start(mode, -70) == ESP_ERR_INVALID_STATE);
  task(NULL);
  assert(!has_grant && !frequency_analyzer_is_running());
}
int main(void) {
  assert(frequency_analyzer_plan_count(FREQUENCY_ANALYZER_COMMON) == 17);
  assert(frequency_analyzer_plan_count(FREQUENCY_ANALYZER_ALL) == 1099);
  for (int mode = 0; mode < FREQUENCY_ANALYZER_MODE_COUNT; mode++) {
    unsigned total = frequency_analyzer_plan_count(mode);
    assert(total);
    for (unsigned i = 0; i < total; i++)
      assert(cc1101_rf_path_for_frequency(frequency_analyzer_plan_frequency(mode, i)) != CC1101_RF_PATH_NONE);
    assert(!frequency_analyzer_plan_frequency(mode, total));
  }
  assert(!frequency_analyzer_plan_count((frequency_analyzer_mode_t)-1));
  assert(!frequency_analyzer_plan_count(FREQUENCY_ANALYZER_MODE_COUNT));
  size_t count;
  const uint32_t *catalog = frequency_catalog_get(&count);
  assert(count == FREQUENCY_CATALOG_CARRIER_COUNT + 1 && catalog[count-1] == 0);
  assert(frequency_analyzer_plan_count(FREQUENCY_ANALYZER_315) == 20);
  assert(frequency_analyzer_plan_count(FREQUENCY_ANALYZER_433) == 20);
  for (unsigned i=0;i<FREQUENCY_CATALOG_CARRIER_COUNT;++i) {
    frequency_analyzer_mode_t mode = FREQUENCY_ANALYZER_CARRIER_FIRST+i;
    assert(frequency_analyzer_plan_count(mode)==1);
    assert(frequency_analyzer_plan_frequency(mode,0)==catalog[i]);
    reset(); target=catalog[i]; run(mode);
    assert(s_snapshot.detections && distance(s_snapshot.frequency_hz,target)<=FREQUENCY_ANALYZER_FINE_STEP_HZ);
  }
  const uint32_t carriers[] = {315000000, 433920000, 868350000, 915000000};
  for (unsigned i = 0; i < 4; i++) {
    reset(); target = carriers[i]; run(FREQUENCY_ANALYZER_COMMON);
    assert(s_snapshot.frequency_hz == target && s_snapshot.detections == 1 && s_snapshot.rssi_dbm > -70);
    assert(s_snapshot.samples >= 49 && s_snapshot.sweep_us == 98000);
  }
  reset(); target = 432123000; run(FREQUENCY_ANALYZER_ALL);
  assert(s_snapshot.detections && distance(s_snapshot.frequency_hz, target) <= FREQUENCY_ANALYZER_FINE_STEP_HZ);
  reset(); target = 300000000; run(FREQUENCY_ANALYZER_COMMON); assert(s_snapshot.frequency_hz == target);
  reset(); target = 928000000; run(FREQUENCY_ANALYZER_915);
  assert(distance(s_snapshot.frequency_hz, target) <= FREQUENCY_ANALYZER_FINE_STEP_HZ);
  reset(); no_signal = true; run(FREQUENCY_ANALYZER_COMMON); assert(!s_snapshot.frequency_hz);
  assert(s_snapshot.samples >= 17 && s_snapshot.sweep_us == 34000);
  reset(); flat_noise = true; run(FREQUENCY_ANALYZER_COMMON); assert(!s_snapshot.frequency_hz);
  reset(); disappear = true; run(FREQUENCY_ANALYZER_COMMON); assert(!s_snapshot.frequency_hz);
  reset(); paused_once = true; run(FREQUENCY_ANALYZER_COMMON); assert(pause_seen);
  reset(); reconfigure = true; run(FREQUENCY_ANALYZER_COMMON); assert(reconfigured && !s_snapshot.frequency_hz);
  reset(); assert(frequency_analyzer_start(FREQUENCY_ANALYZER_COMMON, -70) == ESP_OK);
  frequency_analyzer_stop(); task(NULL); assert(!calls && !has_grant);
  reset(); fail_task = true; assert(frequency_analyzer_start(FREQUENCY_ANALYZER_COMMON, -70) == ESP_ERR_NO_MEM);
  assert(!has_grant && !frequency_analyzer_is_running());
  reset(); busy = true; assert(frequency_analyzer_start(FREQUENCY_ANALYZER_COMMON, -70) == ESP_ERR_INVALID_STATE);
  reset(); assert(frequency_analyzer_start(FREQUENCY_ANALYZER_COMMON, -120) == ESP_ERR_INVALID_ARG);
  reset(); run(FREQUENCY_ANALYZER_COMMON);
  const unsigned success_calls = calls;
  for (unsigned failure = 1; failure <= success_calls; failure++) {
    reset(); fail_at = failure; run(FREQUENCY_ANALYZER_COMMON);
    assert(s_snapshot.error != ESP_OK && s_snapshot.phase == FREQUENCY_ANALYZER_ERROR);
  }
  reset(); stop_after = 4; idle_failures = 3; run(FREQUENCY_ANALYZER_COMMON);
  assert(idle_calls == 4 && s_snapshot.error == ESP_ERR_TIMEOUT);
  printf("PASS: plans, carriers, refinement, noise/transients, pause/cancel, timing budgets and %u injected radio failures\n", success_calls);
}
