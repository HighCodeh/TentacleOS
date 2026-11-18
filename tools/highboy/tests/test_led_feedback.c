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
 if(task_failure)return 0;*(void**)handle=(void*)1;return pdPASS;
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
int main(void) {
 fail_at=3;assert(led_rgb_init()==ESP_FAIL && s_task==NULL);
 fail_at=-1;task_failure=1;assert(led_rgb_init()==ESP_ERR_NO_MEM && s_task==NULL);
 task_failure=0;assert(led_rgb_init()==ESP_OK);
 led_set_signal_config(0xff00ff,0xffff00,0xff0000,40);
 led_set_power_state(LED_POWER_CHARGING);assert(desired_color(0)==scale_color(LED_COLOR_CHARGING,40));
 now=1000;led_signal_info();assert(desired_color(now)==0x660066);
 now+=1000;led_signal_error();assert(desired_color(now)==0x660000);
 int64_t until=s_signal_until;now+=1000;led_signal_info();assert(s_signal_until==until);
 led_signal_error();assert(s_signal_until==until);
 now=until;assert(desired_color(now)==scale_color(LED_COLOR_CHARGING,40));
 led_set_power_state(LED_POWER_FULL);assert(desired_color(now)==scale_color(LED_COLOR_FULL,40));
 led_set_color(12,34,56);assert(desired_color(now)==0x0c2238);
 led_clear();assert(desired_color(now)==scale_color(LED_COLOR_FULL,40));
 led_set_activity(LED_ACTIVITY_NFC,true);
 assert(desired_color(2400000)==0x660066);
 assert(desired_color(2410000)==scale_color(LED_COLOR_FULL,40));
 for(int period=0;period<10;period++) {
  int64_t start=2400000 + period * 100000;
  assert(desired_color(start)==0x660066);
  assert(desired_color(start+9000)==0x660066);
  assert(desired_color(start+10000)==scale_color(LED_COLOR_FULL,40));
  assert(desired_color(start+99000)==scale_color(LED_COLOR_FULL,40));
 }
 led_set_activity(2,true);led_set_activity(LED_ACTIVITY_NFC,false);
 assert(desired_color(2400000)==0x660066);
 led_set_activity(2,false);assert(desired_color(2400000)==scale_color(LED_COLOR_FULL,40));
 led_status_t status;assert(led_get_status(&status)==ESP_OK && status.rgb==0x112233 && status.activity==0);
 led_set_power_state(LED_POWER_BLOCKED);assert(desired_color(2400000)==0x666600);
 assert(desired_color(3400000)==0);
 led_set_signal_config(1,2,3,0);assert(desired_color(2400000)==0);
 led_set_signal_config(0xffffff,2,3,200);now=4000000;led_signal_info();assert(desired_color(now)==0xffffff);
 is_isr=1;now=5000000;led_signal_error();assert(desired_color(now)!=3);
 puts("PASS: LED init failures, priority, bounded pulses, restore power state, dimming/mute and ISR rejection");
}
