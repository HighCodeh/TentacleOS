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
#include "led_control.c"
static int64_t now;
static int writes,fail_at=-1,task_failure,is_isr;
int64_t esp_timer_get_time(void) {return now;}
int xPortInIsrContext(void) {return is_isr;}
void vTaskDelay(int ticks) {(void)ticks;}
unsigned ulTaskNotifyTake(int clear,unsigned ticks) {(void)clear;return ticks;}
void xTaskNotifyGive(void *task) {assert(task);}
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,int stack,void *arg,int priority,void *handle,int core) {
 (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)core;
 if(task_failure) { return 0; }
 *(void**)handle=(void*)1;return pdPASS;
}
const char *esp_err_to_name(esp_err_t err) {(void)err;return "mock";}
i2c_master_bus_handle_t i2c_get_bus(void) {return (void*)1;}
esp_err_t i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,void **out) {
 assert(bus && cfg->device_address==0x2c);*out=(void*)2;return ESP_OK;
}
esp_err_t i2c_master_transmit(void *dev,const void *data,size_t size,int timeout) {
 assert(dev && data && (size==2 || size==4) && timeout==50);
 return writes++==fail_at ? ESP_FAIL:ESP_OK;
}
esp_err_t i2c_master_transmit_receive(void *dev,const void *tx,size_t ntx,void *rx,size_t nrx,int timeout) {
 assert(dev && *(const uint8_t*)tx==0x18 && ntx==1 && nrx==3 && timeout==50);
 uint8_t *rgb=rx;rgb[0]=0x11;rgb[1]=0x22;rgb[2]=0x33;return ESP_OK;
}
#define LEDC_TIMER_0 0
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_CHANNEL_0 0
#define LEDC_TIMER_13_BIT 13
static esp_err_t duty_result, update_result;
static uint32_t backlight_duty;
static int backlight_updates;
static esp_err_t ledc_set_duty(int mode, int channel, uint32_t duty) {
 assert(mode==0 && channel==0);
 if(duty_result==ESP_OK)backlight_duty=duty;
 return duty_result;
}
static esp_err_t ledc_update_duty(int mode, int channel) {
 assert(mode==0 && channel==0);backlight_updates++;
 return update_result;
}
#include "backlight_apply.h"

#define TEST_DISPLAY_TIMEOUT_S 10
#define TEST_DISPLAY_BRIGHTNESS_PCT 80
#define TEST_DISPLAY_START_US 100000000
#define TEST_CHARGE_PERIODS 2
#define TEST_US_PER_MS 1000
#define TEST_MS_PER_SECOND 1000

typedef uint8_t lv_obj_t;
typedef enum { INPUT_BTN_OK, INPUT_BTN_COUNT } input_button_t;
static struct {
 int brightness, auto_lock_seconds;
 bool auto_dim;
} g_config_screen;
static uint32_t s_input_activity_ms;
static bool s_is_button_down, s_is_panel_asleep;
static uint32_t input_last_activity_ms(void) { return s_input_activity_ms; }
static bool input_is_down(input_button_t button) {
 assert(button == INPUT_BTN_OK);
 return s_is_button_down;
}
static const lv_obj_t *lv_screen_active(void) { return NULL; }
static void lcd_display_sleep(bool is_asleep) { s_is_panel_asleep = is_asleep; }
#include "display_policy.h"

static void test_display_sleep_feedback(void) {
 is_isr = 0;
 led_clear();
 led_set_signal_config(s_colors[SIG_INFO], s_colors[SIG_WARNING], s_colors[SIG_ERROR],
                       LED_POWER_BRIGHTNESS_MAX);
 g_config_screen.brightness = TEST_DISPLAY_BRIGHTNESS_PCT;
 g_config_screen.auto_lock_seconds = TEST_DISPLAY_TIMEOUT_S;
 for (int auto_dim = 0; auto_dim <= 1; ++auto_dim) {
  now += TEST_DISPLAY_START_US;
  s_input_activity_ms = (uint32_t)(now / TEST_US_PER_MS);
  s_display_asleep = s_display_dimmed = s_wake_guard = false;
  s_is_button_down = s_is_panel_asleep = false;
  g_config_screen.auto_dim = auto_dim != 0;
  lcd_apply_brightness(TEST_DISPLAY_BRIGHTNESS_PCT);
  led_set_power_state(LED_POWER_CHARGING);
  now += (TEST_DISPLAY_TIMEOUT_S * TEST_MS_PER_SECOND - DISPLAY_DIM_LEAD_MS) *
         TEST_US_PER_MS;
  check_display_inactivity();
  assert(s_display_dimmed == g_config_screen.auto_dim);
  assert(!s_display_asleep && !s_is_panel_asleep && s_display_active);
  assert(desired_color(now) == 0);
  now += DISPLAY_DIM_LEAD_MS * TEST_US_PER_MS;
  check_display_inactivity();
  int64_t sleep_started_us = now;
  for (; now <= sleep_started_us + TEST_CHARGE_PERIODS * LED_CHARGE_PERIOD_US;
       now += POWER_POLL_MS * TEST_US_PER_MS) {
   check_display_inactivity();
   assert(s_display_asleep && s_is_panel_asleep && !s_display_dimmed);
   assert(backlight_duty == 0 && !s_display_active);
   bool is_charge_pulse = (now - sleep_started_us) % LED_CHARGE_PERIOD_US < LED_CHARGE_ON_US;
   assert(desired_color(now) ==
          (is_charge_pulse ? scale_color(LED_COLOR_CHARGING, LED_POWER_BRIGHTNESS_MAX) : 0));
  }
  s_is_button_down = true;
  s_input_activity_ms = (uint32_t)(now / TEST_US_PER_MS);
  check_display_inactivity();
  assert(!s_display_asleep && !s_display_dimmed && !s_is_panel_asleep && s_wake_guard);
  assert(backlight_duty == BL_MAX_DUTY * TEST_DISPLAY_BRIGHTNESS_PCT / MAX_BRIGHTNESS);
  assert(s_display_active && desired_color(now) == 0);
  check_display_inactivity();
  assert(s_wake_guard);
  s_is_button_down = false;
  check_display_inactivity();
  assert(!s_wake_guard);
 }
}

int main(void) {
 fail_at=3;assert(led_rgb_init()==ESP_FAIL && s_task==NULL);
 fail_at=-1;task_failure=1;assert(led_rgb_init()==ESP_ERR_NO_MEM && s_task==NULL);
 task_failure=0;assert(led_rgb_init()==ESP_OK);
 led_set_signal_config(0xff00ff,0xffff00,0xff0000,40);
 now=1000000;
 led_set_power_state(LED_POWER_CHARGING);
 assert(s_display_active && desired_color(now)==0);
 assert(desired_color(now+10000000)==0);
 lcd_apply_brightness(0);
 assert(!s_display_active && backlight_duty==0);
 assert(desired_color(now)==scale_color(LED_COLOR_CHARGING,10));
 int64_t charge_start=now;
 now+=199999;assert(desired_color(now)!=0);
 now++;assert(desired_color(now)==0);
 now=charge_start+9999999;assert(desired_color(now)==0);
 now++;assert(desired_color(now)==scale_color(LED_COLOR_CHARGING,10));
 led_set_power_state(LED_POWER_CHARGING);lcd_apply_brightness(0);
 assert(s_charge_pulse_started_us==charge_start);
 now+=200000;assert(desired_color(now)==0);
 now=charge_start+20000000;assert(desired_color(now)!=0);
 lcd_apply_brightness(5);
 assert(s_display_active && desired_color(now)==0);
 lcd_apply_brightness(255);assert(backlight_duty==8191 && s_display_active);
 int previous_updates=backlight_updates;
 duty_result=ESP_FAIL;lcd_apply_brightness(0);
 assert(s_display_active && backlight_updates==previous_updates);
 duty_result=ESP_OK;update_result=ESP_FAIL;lcd_apply_brightness(0);
 assert(s_display_active && backlight_updates==previous_updates+1);
 update_result=ESP_OK;now=s_signal_until;
 lcd_apply_brightness(0);assert(!s_display_active && desired_color(now)!=0);
 now+=1000;led_signal_info();assert(desired_color(now)==0x660066);
 now+=1000;led_signal_error();assert(desired_color(now)==0x660000);
 int64_t until=s_signal_until;now+=1000;led_signal_info();assert(s_signal_until==until);
 led_signal_error();assert(s_signal_until==until);
 now=until;assert(desired_color(now)==0);
 lcd_apply_brightness(80);
 led_set_power_state(LED_POWER_FULL);
 int64_t full_until=s_full_until_us;
 assert(full_until==now+1000000);
 assert(desired_color(now)==scale_color(LED_COLOR_FULL,10));
 now+=500000;led_set_power_state(LED_POWER_FULL);assert(s_full_until_us==full_until);
 led_set_color(12,34,56);assert(desired_color(now)==0x0c2238);
 led_clear();assert(desired_color(now)==scale_color(LED_COLOR_FULL,10));
 now=full_until;assert(desired_color(now)==0);
 lcd_apply_brightness(0);lcd_apply_brightness(80);
 led_set_power_state(LED_POWER_FULL);assert(desired_color(now)==0 && s_full_until_us==full_until);
 led_set_power_state(LED_POWER_CHARGING);lcd_apply_brightness(0);
 led_set_activity(LED_ACTIVITY_NFC,true);
 now=30000000;
 for(int period=0;period<10;period++) {
  int64_t start=now+period*100000;
  assert(desired_color(start)==0x660066);
  assert(desired_color(start+9000)==0x660066);
  assert(desired_color(start+10000)==0);
  assert(desired_color(start+99000)==0);
 }
 led_set_power_state(LED_POWER_FULL);
 assert(desired_color(now)==0x660066 && desired_color(now+10000)==0);
 led_signal_warning();assert(desired_color(now)==0x666600);
 led_signal_error();assert(desired_color(now)==0x660000);
 now=s_signal_until;led_set_activity(2,true);led_set_activity(LED_ACTIVITY_NFC,false);
 assert(desired_color(31000000)==0x660066);
 led_set_activity(2,false);
 now=31000000;assert(desired_color(now)==0);
 led_set_power_state(LED_POWER_CHARGING);lcd_apply_brightness(80);lcd_apply_brightness(0);
 assert(desired_color(now)==scale_color(LED_COLOR_CHARGING,10));
 led_set_signal_config(0xff00ff,0xffff00,0xff0000,4);
 assert(desired_color(now)==scale_color(LED_COLOR_CHARGING,4));
 led_set_signal_config(0xff00ff,0xffff00,0xff0000,0);assert(desired_color(now)==0);
 led_set_power_state(LED_POWER_FULL);assert(desired_color(now)==0);
 led_set_signal_config(0xff00ff,0xffff00,0xff0000,40);
 now=s_full_until_us;assert(desired_color(now)==0);
 led_set_power_state(LED_POWER_CHARGING);led_set_power_state(LED_POWER_FULL);
 assert(desired_color(now)==scale_color(LED_COLOR_FULL,10));
 led_set_power_state(LED_POWER_OFF);assert(desired_color(now)==0);
 led_status_t status;assert(led_get_status(&status)==ESP_OK && status.rgb==0x112233 && status.activity==0);
 led_set_power_state(LED_POWER_BLOCKED);now=60000000;
 assert(desired_color(now)==0x666600 && desired_color(now+1000000)==0);
 led_set_power_state(LED_POWER_LOW);assert(desired_color(now)==0x660000);
 led_set_signal_config(0xffffff,2,3,200);led_signal_info();assert(desired_color(now)==0xffffff);
 led_power_state_t previous_power=s_power;bool previous_display=s_display_active;
 is_isr=1;now+=1000000;led_signal_error();
 led_set_power_state(LED_POWER_CHARGING);led_set_display_active(!previous_display);
 led_set_activity(LED_ACTIVITY_NFC,true);
 assert(s_power==previous_power && s_display_active==previous_display && s_activity==0);
 assert(desired_color(now)!=3);
 test_display_sleep_feedback();
 puts("PASS: LED charge screen gating, 200ms/10s cadence, one-shot full, backlight failures, app priority, dimming/mute and ISR rejection");
 puts("PASS: display sleep preserves charging pulses across repeated policy ticks, with/without auto-dim and guarded input wake");
}
