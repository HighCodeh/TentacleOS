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
#include "../../firmware_p4/components/Service/power_manager/usb_power.c"

static int64_t now_us;
static bool locked, fail_alloc, fail_task, host_mode;
bool husb320_host_mode(void) { return host_mode; }
static int warnings, infos, init_calls, creates, writes, fail_writes;
static husb320_source_t source;
static esp_err_t source_result;
static uint16_t limit, first_requested;
const char *esp_err_to_name(int error) { (void)error;return "fixture"; }
int64_t esp_timer_get_time(void) { return now_us; }
void *xSemaphoreCreateMutex(void) { return fail_alloc ? NULL : (void *)1; }
int xSemaphoreTake(void *handle,uint32_t ticks) { (void)ticks;assert(handle);if(locked)return 0;locked=true;return pdTRUE; }
void xSemaphoreGive(void *handle) { assert(handle && locked);locked=false; }
void vTaskDelay(uint32_t ticks) { (void)ticks;assert(false); }
int xTaskCreatePinnedToCore(void (*task)(void *),const char *name,uint32_t stack,void *ctx,int priority,void **out,int core) {
  (void)name;assert(task==worker && stack>=2048 && !ctx && priority==SYS_PRIO_SERVICE_HI && core==SYS_CORE_RADIO);
  creates++;if(fail_task)return 0;*out=(void *)2;return pdPASS;
}
void led_signal_warning(void) { warnings++; }
void led_signal_info(void) { infos++; }
void led_signal_error(void) { warnings++; }
int husb320_read_source(husb320_source_t *out) { *out=source;return source_result; }
int husb320_init(void) { assert(limit<=500);init_calls++;return ESP_OK; }
int bq25896_set_usb_input_limit(uint16_t requested,uint16_t *out) {
  assert(requested==500 || requested==900);if(writes++==0)first_requested=requested;*out=0;
  if(fail_writes) {fail_writes--;return ESP_FAIL;}limit=requested;*out=requested;return ESP_OK;
}
static void reset(void) {
  s_lock=NULL;s_task=NULL;s_snapshot=(usb_power_snapshot_t){0};s_updated_us=0;
  s_stable_since_us=s_retry_at_us=s_report_at_us=0;
  s_has_candidate=s_has_snapshot=s_has_report_pending=false;
  locked=fail_alloc=fail_task=false;warnings=infos=init_calls=creates=writes=fail_writes=0;
  now_us=0;limit=0;source=(husb320_source_t){0};source_result=ESP_OK;
}
int main(void) {
  reset();usb_power_snapshot_t snapshot;
  assert(!usb_power_get(&snapshot) && !usb_power_get(NULL));
  fail_writes=1;assert(usb_power_init()==ESP_FAIL && !creates);fail_writes=0;
  fail_alloc=true;assert(usb_power_init()==ESP_ERR_NO_MEM && !creates && limit==500);
  fail_alloc=false;fail_task=true;assert(usb_power_init()==ESP_ERR_NO_MEM && !s_task && limit==500);
  fail_task=false;assert(usb_power_init()==ESP_OK);int count=creates;
  assert(usb_power_init()==ESP_OK && creates==count);
  assert(update_input()==250 && limit==500 && usb_power_get(&snapshot) && !snapshot.is_attached);
  source=(husb320_source_t){.is_attached=true};assert(update_input()==250 && limit==500);
  source.advertised_ma=1500;
  assert(update_input()==250 && limit==500);
  now_us=249999;assert(update_input()==250 && limit==500);
  now_us=250000;assert(update_input()==20 && limit==900);
  assert(usb_power_get(&snapshot) && snapshot.advertised_ma==1500 && snapshot.applied_ma==900);
  source.advertised_ma=3000;assert(update_input()==20 && limit==900); // board cap
  source.advertised_ma=0;assert(update_input()==250 && limit==500); // immediate downgrade
  source.advertised_ma=3000;assert(update_input()==250 && limit==500); // requires new stability
  now_us+=250000;assert(update_input()==20 && limit==900);
  source_result=ESP_FAIL;source.last_reg=0x02;source.advertised_ma=3000;writes=0;
  assert(update_input()==250 && first_requested==500 && limit==500 && init_calls==1);
  assert(usb_power_get(&snapshot) && !snapshot.is_source_valid && !snapshot.advertised_ma);
  assert(snapshot.source_error==ESP_FAIL && snapshot.source_reg==0x02 && snapshot.input_error==ESP_OK);
  update_input();assert(init_calls==1);now_us+=2000000;update_input();assert(init_calls==2);
  source_result=ESP_OK;source.last_reg=0;source.advertised_ma=1500;update_input();now_us+=250000;update_input();assert(limit==900);
  fail_writes=1;assert(update_input()==20 && limit==500); // failed high write, verified fallback
  assert(usb_power_get(&snapshot) && snapshot.input_error!=ESP_OK && snapshot.applied_ma==500);
  assert(snapshot.source_error==ESP_OK && snapshot.source_reg==0);
  update_input();assert(limit==500);now_us+=250000;update_input();assert(limit==900);
  fail_writes=2;update_input();assert(usb_power_get(&snapshot) && snapshot.applied_ma==0 && snapshot.input_error!=ESP_OK);
  source=(husb320_source_t){0};assert(update_input()==250 && limit==500);
  now_us+=1000001;assert(!usb_power_get(&snapshot));
  locked=true;assert(!usb_power_get(&snapshot));locked=false;
  assert(warnings+infos<=3); // event feedback is rate limited with logs
  puts("PASS: USB source transitions, 900 mA cap, immediate fallback, ADC-independent policy, stale state and allocation/control failures");
  host_mode=true;
  int writes_before=writes, init_before=init_calls;
  update_input();
  assert(writes==writes_before && init_calls==init_before);
  return 0;
}
