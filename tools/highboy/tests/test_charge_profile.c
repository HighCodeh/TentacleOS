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


#include "bq25896.c"

#include <assert.h>
#include <stdio.h>

static uint8_t s_regs[BQ25896_REGISTER_COUNT];
static bool s_locked, s_fail_lock, s_stall_adc;
static int s_fail_read = -1, s_fail_write = -1;
static unsigned s_adc_reads, s_conversions, s_drop_on_conversion;
static int64_t s_now_us;

const char *esp_err_to_name(int error) { (void)error; return "fixture"; }
static bool s_input_locked;
static bool s_bad_input_readback;
void *xSemaphoreCreateMutex(void) { return (void *)2; }
int xSemaphoreTake(void *handle, uint32_t ticks) {
  (void)ticks;
  if (handle==(void *)2) { if(s_input_locked) return 0; s_input_locked=true;return pdTRUE; }
  assert(handle && !s_locked);
  if (s_fail_lock) return 0;
  s_locked=true; return pdTRUE;
}
void xSemaphoreGive(void *handle) { if(handle==(void *)2) {assert(s_input_locked);s_input_locked=false;return;} assert(handle && s_locked);s_locked=false; }
int64_t esp_timer_get_time(void) { return s_now_us; }
void vTaskDelay(uint32_t ticks) { s_now_us += ticks*1000; }
int gpio_config(const gpio_config_t *config) { (void)config;return ESP_OK; }
int gpio_set_level(int pin,int level) { (void)pin;(void)level;return ESP_OK; }
int gpio_get_level(int pin) { (void)pin;return 0; }
void *i2c_get_bus(void) { return (void *)1; }
int i2c_bus_recover(void) { return ESP_OK; }
int i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,void **out) {
  (void)bus;(void)cfg;*out=(void *)1;return ESP_OK;
}
int i2c_master_transmit(void *device,const uint8_t *data,size_t length,int timeout) {
  assert(device==s_dev && (s_locked || (s_input_locked && data[0]==REG00_INPUT)) && length==2 && timeout>0);
  if (data[0]==s_fail_write) return ESP_FAIL;
  s_regs[data[0]]=data[1];
  if (data[0]==REG02_ADC_CONTROL && (data[1]&REG02_CONV_START)) {
    s_adc_reads=0;s_conversions++;
    if (s_conversions==s_drop_on_conversion) s_regs[REG0E_BATTERY_ADC]=34; // 2984 mV
  }
  return ESP_OK;
}
int i2c_master_transmit_receive(void *device,const uint8_t *reg,size_t count,
                                uint8_t *out,size_t length,int timeout) {
  assert(device==s_dev && (s_locked || (s_input_locked && *reg==REG00_INPUT)) && count==1 && length==1 && timeout>0);
  if (*reg==s_fail_read) return ESP_FAIL;
  if (*reg==REG02_ADC_CONTROL && (s_regs[*reg]&REG02_CONV_START) &&
      ++s_adc_reads>=2 && !s_stall_adc) s_regs[*reg]&=(uint8_t)~REG02_CONV_START;
  *out=s_regs[*reg];if(s_bad_input_readback && *reg==REG00_INPUT) *out^=1;return ESP_OK;
}
static void reset(void) {
  s_input_locked=s_bad_input_readback=false;s_input_lock=(void *)2;s_is_input_isolated=false;s_has_external_input_hiz=false;s_present=false;
  memset(s_regs,0,sizeof(s_regs));
  s_regs[REG03_POWER_CONTROL]=0x0a; // SYS_MIN 3500 mV, charge and OTG off
  s_regs[REG04_CHARGE_CURRENT]=6;
  s_regs[REG05_PRE_TERM_CURRENT]=0x10;
  s_regs[REG06_CHARGE_VOLTAGE]=0x56;
  s_regs[REG07_TIMER_CONTROL]=REG07_EN_TERM|REG07_EN_TIMER;
  s_regs[REG0B_STATUS]=0x24;
  s_regs[REG0E_BATTERY_ADC]=36; // Reported incident: 3024 mV
  s_regs[REG10_TS_ADC]=79;
  s_regs[REG11_VBUS_ADC]=0x99;
  s_regs[REG14_PART_INFO]=0x06;
  s_dev=(void *)1;s_lock=(void *)1;
  s_is_recovery_required=false; // Only the test may simulate a new boot.
  s_locked=s_fail_lock=s_stall_adc=false;
  s_fail_read=s_fail_write=-1;
  s_adc_reads=s_conversions=s_drop_on_conversion=0;s_now_us=0;
}
static bool enabled(void) { return (s_regs[REG03_POWER_CONTROL]&REG03_CHG_CONFIG)!=0; }
static void expect_blocked(void) {
  assert(bq25896_set_charge_enable_for_profile(true,POWER_PROFILE_CHARGE_604858)!=ESP_OK);
  assert(!enabled() && !s_locked);
}
int main(void) {
  reset();
  assert(bq25896_set_charge_enable(true)==ESP_ERR_INVALID_STATE && !enabled());
  assert(bq25896_set_charge_enable_for_profile(true,POWER_PROFILE_CHARGE_604858)==ESP_OK);
  assert(enabled());
  assert(s_regs[REG04_CHARGE_CURRENT]==6 && s_regs[REG06_CHARGE_VOLTAGE]==0x56);
  assert(s_regs[REG05_PRE_TERM_CURRENT]==0x10 && s_regs[REG07_TIMER_CONTROL]==0x88);
  assert(s_regs[REG03_POWER_CONTROL]==0x1a); // Input path, SYS_MIN and OTG unchanged.
  assert(bq25896_set_charge_enable_for_profile(false,POWER_PROFILE_CHARGE_604858)==ESP_OK && !enabled());

  for(unsigned adc=35;adc<=44;adc++) { // 3004..3184 mV: explicit model only
    reset();s_regs[REG0E_BATTERY_ADC]=adc;
    assert(bq25896_set_charge_enable(true)!=ESP_OK && !enabled());
    assert(bq25896_set_charge_enable_for_profile(true,POWER_PROFILE_CHARGE_604858)==ESP_OK && enabled());
  }
  reset();s_regs[REG0E_BATTERY_ADC]=45; // 3204 mV: compatibility policy still works
  assert(bq25896_set_charge_enable(true)==ESP_OK && enabled());

  reset();bq25896_telem_t sample;
  assert(bq25896_read_telemetry(&sample)==ESP_OK);
  sample.vbat_mv=3000;
  assert(!pack_sample_usable(&sample,POWER_PROFILE_CHARGE_604858));
  sample.vbat_mv=3001;
  assert(pack_sample_usable(&sample,POWER_PROFILE_CHARGE_604858));
  assert(!pack_sample_usable(&sample,POWER_PROFILE_CHARGE_GUARDED));
  assert(!pack_sample_usable(&sample,POWER_PROFILE_CHARGE_COUNT));

  reset();s_regs[REG0E_BATTERY_ADC]=34;expect_blocked();assert(s_is_recovery_required);
  s_regs[REG0E_BATTERY_ADC]=65; // 3604 mV rebound must not clear recovery
  expect_blocked();assert(bq25896_set_charge_enable(true)!=ESP_OK && !enabled());
  assert(bq25896_stop_charge()==ESP_OK);expect_blocked();
  assert(bq25896_read_telemetry(&sample)==ESP_OK && sample.is_recovery_required);

  reset();s_regs[REG0E_BATTERY_ADC]=0;expect_blocked();assert(!s_is_recovery_required);
  reset();s_regs[REG0E_BATTERY_ADC]=96;expect_blocked(); // Above 4200 mV
  reset();s_regs[REG0B_STATUS]=0;s_regs[REG11_VBUS_ADC]=0;expect_blocked();
  reset();s_regs[REG10_TS_ADC]=0;expect_blocked();
  reset();s_regs[REG10_TS_ADC]=127;expect_blocked();
  reset();s_regs[REG0C_FAULT]=8;expect_blocked();
  reset();s_regs[REG0C_FAULT]=FAULT_WATCHDOG;expect_blocked();assert(!s_is_recovery_required);
  reset();s_regs[REG03_POWER_CONTROL]|=REG03_OTG_CONFIG;expect_blocked();
  reset();s_regs[REG07_TIMER_CONTROL]&=(uint8_t)~REG07_EN_TIMER;expect_blocked();
  reset();s_regs[REG12_CHARGE_ADC]=11;expect_blocked();
  reset();s_stall_adc=true;expect_blocked();
  reset();s_fail_lock=true;expect_blocked();
  reset();s_fail_read=REG0E_BATTERY_ADC;expect_blocked();
  reset();s_regs[REG04_CHARGE_CURRENT]=7;s_fail_write=REG04_CHARGE_CURRENT;expect_blocked();
  reset();s_fail_write=REG03_POWER_CONTROL;expect_blocked();
  reset();s_drop_on_conversion=2;expect_blocked();assert(s_is_recovery_required);
  reset();s_regs[REG03_POWER_CONTROL]|=REG03_CHG_CONFIG;
  assert(bq25896_set_charge_enable_for_profile(true,POWER_PROFILE_CHARGE_COUNT)==ESP_ERR_INVALID_ARG);
  assert(!enabled());
  reset();s_regs[REG00_INPUT]=0xbf;
  assert(bq25896_init()==ESP_OK && s_regs[REG00_INPUT]==0x48); // boot clamps retained input
  uint16_t applied=0;
  uint8_t before[BQ25896_REGISTER_COUNT];memcpy(before,s_regs,sizeof(before));
  s_locked=true; // ADC owner remains busy: the input path must still work.
  assert(bq25896_set_usb_input_limit(900,&applied)==ESP_OK && applied==900 && s_locked);
  assert(s_regs[REG00_INPUT]==0x50 && !s_input_locked);
  assert(!memcmp(before+1,s_regs+1,sizeof(before)-1)); // cell charge and all other registers untouched
  assert(bq25896_set_usb_input_limit(500,&applied)==ESP_OK && applied==500 && s_regs[0]==0x48);
  s_locked=false;
  assert(bq25896_set_usb_input_limit(1000,&applied)==ESP_ERR_INVALID_ARG && applied==0);
  assert(bq25896_set_usb_input_limit(900,NULL)==ESP_ERR_INVALID_ARG);
  s_input_locked=true;assert(bq25896_set_usb_input_limit(500,&applied)==ESP_ERR_TIMEOUT);s_input_locked=false;
  s_fail_read=REG00_INPUT;
  assert(bq25896_set_usb_input_limit(900,&applied)!=ESP_OK && applied==0 && s_regs[0]==0xc8);
  s_fail_read=-1;assert(bq25896_set_usb_input_limit(500,&applied)==ESP_OK && applied==500 && s_regs[0]==0x48);
  s_fail_write=REG00_INPUT;
  assert(bq25896_set_usb_input_limit(900,&applied)!=ESP_OK && applied==0);
  s_fail_write=-1;assert(bq25896_set_usb_input_limit(500,&applied)==ESP_OK && applied==500);
  s_bad_input_readback=true;
  assert(bq25896_set_usb_input_limit(900,&applied)==ESP_ERR_INVALID_RESPONSE && applied==0 && s_regs[0]==0xc8);
  s_bad_input_readback=false;assert(bq25896_set_usb_input_limit(500,&applied)==ESP_OK);
  s_regs[0]=0xc8; // HIZ from another owner must remain set.
  assert(bq25896_set_usb_input_limit(900,&applied)==ESP_OK && applied==0 && (s_regs[0]&0x80));
  s_fail_read=REG00_INPUT;
  assert(bq25896_set_usb_input_limit(500,&applied)!=ESP_OK && applied==0);
  s_fail_read=-1;
  assert(bq25896_set_usb_input_limit(900,&applied)==ESP_OK && applied==0 && (s_regs[0]&0x80));
  assert(bq25896_set_input_current_limit(500,&applied)==ESP_ERR_NOT_SUPPORTED);
  puts("PASS: actual charger profile at 3024 mV, generic floor, boundaries, latched low-cell recovery, fresh ADC, faults and I2C failure stops");
}
