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
#include "cc1101.h"
#include "resource_mgr.h"
#include "subghz_spectrum.c"
static bool s_is_busy,s_has_grant,s_fail_mutex,s_fail_take,s_fail_task,s_cancel_start,s_stop_during_sweep;
static unsigned s_samples,s_strobes;
static unsigned s_radio_calls, s_radio_fail, s_idle_failures, s_cleanup_retries;
static esp_err_t radio_result(void) {
  return ++s_radio_calls == s_radio_fail ? ESP_FAIL : ESP_OK;
}
static void (*s_worker)(void *);
esp_err_t resource_acquire(const res_request_t *request,res_handle_t *out) {
  assert(request->id==RES_SUBGHZ && !request->allow_preempt);
  if (s_is_busy || s_has_grant) {
    return ESP_ERR_INVALID_STATE;
  }
  s_has_grant=true;*out=42;return ESP_OK;
}
esp_err_t resource_release(res_handle_t grant) { assert(s_has_grant && grant==42);s_has_grant=false;return ESP_OK; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return s_fail_mutex?NULL:(void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t timeout) { assert(mutex!=NULL);return !s_fail_take; }
void xSemaphoreGive(SemaphoreHandle_t mutex) { assert(mutex!=NULL); }
BaseType_t xTaskCreatePinnedToCore(void (*task)(void *),const char *name,uint32_t stack,void *argument,int priority,TaskHandle_t *handle,int core) {
  assert(s_has_grant && priority==SYS_PRIO_BACKGROUND_LO && core==SYS_CORE_RADIO);
  if (s_cancel_start) {
    subghz_spectrum_stop();
  }
  if (s_fail_task) {
    return pdFALSE;
  }
  s_worker=task;return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { assert(!s_has_grant); }
esp_err_t cc1101_init(void) { assert(s_has_grant); return radio_result(); }
esp_err_t cc1101_set_preset(cc1101_preset_t preset, uint32_t freq) {
  assert(s_has_grant);
  if (preset == CC1101_PRESET_IDLE && s_idle_failures) {
    --s_idle_failures;
    return ESP_ERR_TIMEOUT;
  }
  return radio_result();
}
static cc1101_radio_config_t s_expected_radio;
static bool s_expect_radio;
esp_err_t cc1101_set_radio_config(const cc1101_radio_config_t *radio, uint32_t freq) {
  assert(s_has_grant && s_expect_radio && freq == 314000000);
  assert(memcmp(radio, &s_expected_radio, sizeof(*radio)) == 0);
  s_expect_radio = false;
  return radio_result();
}
esp_err_t cc1101_enter_rx_mode(void) { assert(s_has_grant); return radio_result(); }
void led_signal_error(void) {}
esp_err_t cc1101_strobe(uint8_t command) { assert(s_has_grant);++s_strobes;
  return ESP_OK;
}
esp_err_t cc1101_set_frequency(uint32_t frequency) { assert(s_has_grant);
  return radio_result();
}
uint8_t cc1101_read_reg(uint8_t reg) {
  assert(s_has_grant);++s_samples;if(s_stop_during_sweep)subghz_spectrum_stop();return 42;
}
esp_err_t cc1101_get_register(uint8_t reg, uint8_t *out) { esp_err_t error = radio_result(); if (error == ESP_OK) *out = cc1101_read_reg(reg); return error; }
float cc1101_convert_rssi(uint8_t raw) { return -42; }
void ets_delay_us(uint32_t delay) {}
int64_t esp_timer_get_time(void) { return 1; }
void vTaskDelay(TickType_t ticks) {
  assert(s_has_grant);
  if(ticks==SPECTRUM_CLEANUP_RETRY_MS) ++s_cleanup_retries;
  if(ticks==SWEEP_DELAY_MS)subghz_spectrum_stop();
}
static void run(bool cancelled,bool partial) {
  s_cancel_start=cancelled;s_stop_during_sweep=partial;s_worker=NULL;s_samples=0;
  subghz_spectrum_start(433920000,1000000);assert(s_worker!=NULL && s_has_grant);
  void (*worker)(void *)=s_worker;
  subghz_spectrum_start(433920000,1000000);assert(s_worker==worker);
  worker(NULL);assert(!s_has_grant && s_state==SUBGHZ_SPECTRUM_IDLE);
  subghz_spectrum_line_t line;assert(subghz_spectrum_get_line(&line));
  if(cancelled || partial)assert(line.timestamp==0);
  else assert(line.timestamp==1 && s_samples==SPECTRUM_SAMPLES*RSSI_SAMPLE_COUNT);
  if(cancelled)assert(s_samples==0);
}
int main(void) {
  assert(cc1101_config_from_preset(CC1101_PRESET_OOK_650KHZ, &s_expected_radio) == ESP_OK);
  assert(cc1101_config_step(&s_expected_radio, CC1101_CONFIG_LNA_GAIN, 1) == ESP_OK);
  s_expect_radio = true;
  assert(subghz_spectrum_start_configured(315000000, 2000000, &s_expected_radio) == ESP_OK);
  s_worker(NULL);
  assert(!s_expect_radio && !s_has_grant);
  s_worker = NULL;
  s_spectrum_mutex = NULL;

  subghz_spectrum_stop();assert(s_state==SUBGHZ_SPECTRUM_IDLE);
  s_is_busy=true;subghz_spectrum_start(433920000,1000000);assert(!s_has_grant && s_worker==NULL);s_is_busy=false;
  s_fail_mutex=true;subghz_spectrum_start(433920000,1000000);assert(!s_has_grant && s_worker==NULL);s_fail_mutex=false;
  s_fail_take=true;subghz_spectrum_start(433920000,1000000);assert(!s_has_grant && s_worker==NULL);s_fail_take=false;
  s_fail_task=true;subghz_spectrum_start(433920000,1000000);assert(!s_has_grant && s_worker==NULL);s_fail_task=false;
  run(true,false);run(false,true);run(false,false);
  assert(subghz_spectrum_start(0, 1000000) == ESP_ERR_INVALID_ARG);
  assert(subghz_spectrum_start(433920000, 0) == ESP_ERR_INVALID_ARG);
  assert(subghz_spectrum_start(370000000, 100000000) == ESP_ERR_INVALID_ARG);
  s_stop_during_sweep = false;
  for (unsigned call = 1; call <= 7; ++call) {
    s_radio_calls = 0; s_radio_fail = call; s_samples = 0;
    s_idle_failures = 2; s_cleanup_retries = 0;
    assert(subghz_spectrum_start(433920000, 1000000) == ESP_OK);
    s_worker(NULL);
    assert(!s_has_grant && s_state == SUBGHZ_SPECTRUM_IDLE);
    assert(subghz_spectrum_get_error() == ESP_FAIL && s_cleanup_retries == (call == 1 ? 0 : 2));
    subghz_spectrum_line_t line; assert(subghz_spectrum_get_line(&line));
    assert(line.timestamp == 0);
  }
  s_radio_fail = 0; s_idle_failures = 0;
  run(false, false); assert(subghz_spectrum_get_error() == ESP_OK);
  puts("PASS: spectrum radio reservation, startup/stop races, allocation/task errors and partial-sweep rejection, radio error propagation and retained IDLE cleanup");
}
