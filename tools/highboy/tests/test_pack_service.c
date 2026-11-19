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
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "battery_service.c"
#include "voltage_curve.h"

static jmp_buf done;
static bq25896_telem_t input;
static bq27220_sample_t gauge;
static esp_err_t read_error, gauge_error;
static void (*after_poll)(void);
static int enables, disables;
static led_power_state_t power_state;
void vTaskDelay(int ticks) {
 (void)ticks;
 if (after_poll != NULL) after_poll();
 else longjmp(done, 1);
}
void vTaskDelete(void *task) { (void)task; }
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,int stack,void *arg,int priority,void *handle,int core) {
 (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)core;return pdPASS;
}
const char *esp_err_to_name(esp_err_t err) {(void)err;return "mock";}
esp_err_t bq25896_read_telemetry(bq25896_telem_t *out) {*out=input;out->soc=bq25896_get_battery_percentage(input.vbat_mv);return read_error;}
esp_err_t bq27220_read_sample(bq27220_sample_t *out) {*out=gauge;return gauge_error;}
esp_err_t bq25896_set_charge_enable(bool enabled) {
 if(enabled)++enables;else ++disables;input.charge_enabled=enabled;return ESP_OK;
}
esp_err_t bq25896_stop_charge(void) {++disables;return ESP_OK;}
esp_err_t bq25896_reapply_runtime_config(void) {return ESP_OK;}
bool bq25896_is_present(void) {return true;}
void led_set_power_state(led_power_state_t state) {power_state=state;}
void led_signal_info(void) {}
void led_signal_warning(void) {}
static void poll(void) {
 if(!setjmp(done)) battery_pack_task(NULL);
}
static unsigned sequence_step;
static void check_sequence(void) {
 if (sequence_step == 181 || sequence_step == 184) {
  assert(s_snap.soc == -1 && !s_snap.is_soc_estimated);
 } else {
  assert(s_snap.valid && s_snap.present && s_snap.soc == 50 && s_snap.is_soc_estimated);
  assert(s_charge.soc == 50 && !s_charge.gauge_valid);
 }
 switch (sequence_step++) {
  case 0: input.vbat_mv=3900;input.power_good=true;input.charging=true;break;
  case 60: input.vbat_mv=3700;input.power_good=false;input.charging=false;break;
  case 180: input.adc_valid=false;break;
  case 181: input.adc_valid=true;break;
  case 182: gauge_error=ESP_FAIL;break;
  case 183: input.vbat_mv=2304;break;
  case 184: input.vbat_mv=3700;break;
  case 185: longjmp(done, 1);break;
 }
}
int main(void) {
 assert(battery_service_capacity_mah()==2450);
 assert(battery_service_set_capacity(2000)==ESP_OK && battery_service_capacity_mah()==2000);
 assert(battery_service_set_capacity(3000)==ESP_ERR_INVALID_ARG && battery_service_capacity_mah()==2000);
 assert(battery_service_set_capacity(2450)==ESP_OK && battery_service_capacity_mah()==2450);
 s_mtx=(void*)1;s_charge_control=(void*)2;s_started=true;
 input=(bq25896_telem_t){.adc_valid=true,.power_good=true,.vbat_mv=3004,
    .ts_milli_pct=55000,.charge_current_limit_ma=384,.charge_voltage_limit_mv=4176};
 gauge=(bq27220_sample_t){.voltage_mv=2992,.design_mah=3000,.full_mah=3000,
   .remaining_mah=139,.soc_pct=5,.current_ma=0};
 poll();assert(s_snap.valid && s_snap.present && s_snap.vbus_present && s_snap.soc==0 && s_snap.is_soc_estimated);
 assert(s_charge.current_valid && !s_charge.gauge_valid && !enables);
 assert(power_state==LED_POWER_BLOCKED);
 input.vbat_mv=3600;gauge.voltage_mv=3600;input.charging=true;input.chg=CHARGE_STATUS_FAST_CHARGE;
 poll();assert(enables==1 && s_snap.charging && s_snap.soc==40 && s_snap.is_soc_estimated);
 assert(power_state==LED_POWER_CHARGING);
 gauge.design_mah=2000;gauge.full_mah=2000;gauge.soc_pct=40;
 poll();assert(s_snap.soc==40 && !s_charge.gauge_valid && s_snap.is_soc_estimated);
 gauge.design_mah=2450;gauge.full_mah=2450;
 poll();assert(s_snap.soc==40 && !s_charge.gauge_valid && s_snap.is_soc_estimated); // Capacity alone is not calibration.
 input.charge_current_limit_ma=448;poll();assert(!input.charge_enabled && disables==1);
 input.charge_current_limit_ma=384;input.vbat_mv=2990;input.charge_enabled=true;
 input.charging=false;input.chg=CHARGE_STATUS_NOT_CHARGING;poll();assert(!input.charge_enabled);
 input.vbat_mv=3600;input.ts_milli_pct=49000;poll();assert(enables==1);
 input.ts_milli_pct=55000;input.fault_live=8;poll();assert(enables==1);
 input.fault_live=0;input.otg_enabled=true;poll();assert(enables==1);
 input.otg_enabled=false;input.adc_valid=false;poll();assert(!s_snap.valid && s_snap.soc==-1 && enables==1);
 input.adc_valid=true;read_error=ESP_FAIL;poll();assert(!s_snap.valid && !s_snap.charging && s_snap.soc==-1);
 read_error=ESP_OK;input.power_good=false;input.vbat_mv=3100;gauge.design_mah=3000;
 poll();assert(s_snap.valid && s_snap.low && s_snap.soc==0 && s_snap.is_soc_estimated && power_state==LED_POWER_LOW);
 input.vbat_mv=3700;poll();assert(!s_snap.low && s_snap.soc==50 && power_state==LED_POWER_OFF);
 input.power_good=true;s_charge_requested=false;poll();assert(enables==1);
 input=(bq25896_telem_t){.adc_valid=true,.vbat_mv=3700,.ts_milli_pct=55000,
    .charge_current_limit_ma=384,.charge_voltage_limit_mv=4176};
 gauge.soc_pct=99;s_charge_requested=true;after_poll=check_sequence;poll();after_poll=NULL;
 assert(sequence_step==186);
 puts("PASS: V203 voltage estimate, USB transition filtering, invalid/missing/gauge-failed samples, independent charge guards and low-cell alerts");
}
