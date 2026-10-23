// SPDX-License-Identifier: GPL-3.0-or-later
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
void cc1101_strobe(uint8_t command) { assert(s_has_grant);++s_strobes; }
void cc1101_set_frequency(uint32_t frequency) { assert(s_has_grant); }
uint8_t cc1101_read_reg(uint8_t reg) {
  assert(s_has_grant);++s_samples;if(s_stop_during_sweep)subghz_spectrum_stop();return 42;
}
float cc1101_convert_rssi(uint8_t raw) { return -42; }
void ets_delay_us(uint32_t delay) {}
int64_t esp_timer_get_time(void) { return 1; }
void vTaskDelay(TickType_t ticks) {
  assert(s_has_grant);
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
  subghz_spectrum_stop();assert(s_state==SUBGHZ_SPECTRUM_IDLE);
  s_is_busy=true;subghz_spectrum_start(433920000,1000000);assert(!s_has_grant && s_worker==NULL);s_is_busy=false;
  s_fail_mutex=true;subghz_spectrum_start(433920000,1000000);assert(!s_has_grant && s_worker==NULL);s_fail_mutex=false;
  s_fail_take=true;subghz_spectrum_start(433920000,1000000);assert(!s_has_grant && s_worker==NULL);s_fail_take=false;
  s_fail_task=true;subghz_spectrum_start(433920000,1000000);assert(!s_has_grant && s_worker==NULL);s_fail_task=false;
  run(true,false);run(false,true);run(false,false);
  puts("PASS: spectrum radio reservation, startup/stop races, allocation/task errors and partial-sweep rejection");
}
