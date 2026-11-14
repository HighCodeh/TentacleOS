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
#include <string.h>
#include "../../firmware_p4/components/Drivers/input_manager/input_manager.c"

static uint64_t pressed, configured, wake;
static int64_t clock_us;
static input_event_t queue[128];
static unsigned head, tail;
int64_t esp_timer_get_time(void) { return clock_us; }
int gpio_get_level(int pin) { return !(pressed & (UINT64_C(1)<<pin)); }
int gpio_config(const gpio_config_t *cfg) { assert(cfg->mode==GPIO_MODE_INPUT && cfg->pull_up_en==1); configured=cfg->pin_bit_mask; return ESP_OK; }
int gpio_wakeup_enable(int pin,int mode) { assert(mode==GPIO_INTR_LOW_LEVEL); wake|=UINT64_C(1)<<pin; return ESP_OK; }
int esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }
void *xQueueCreate(int count,size_t size) { assert(count==16 && size==sizeof(input_event_t)); return queue; }
int xQueueSend(void *q,const void *item,int ticks) { assert(q==queue && ticks==0 && tail<128); queue[tail++]=*(const input_event_t*)item; return 1; }
int xQueueReceive(void *q,void *item,unsigned ticks) { (void)ticks; assert(q==queue); if(head==tail) return 0; *(input_event_t*)item=queue[head++]; return 1; }
void vQueueDelete(void *q) { assert(q==queue); }

#define POWER_POLICY_BACK_HOLD_MS 5000
static uint32_t s_back_pressed_at;
static bool s_back_fired, s_back_tracking;
static unsigned dialogs;
static uint32_t lv_tick_get(void) { return clock_us/1000; }
static bool msgbox_is_open(void) { return false; }
static void power_policy_show_shutdown_dialog(void) { dialogs++; }
#include "gestures.inc"

static void settle(uint64_t pins) {
  pressed=pins;
  for(int i=0;i<4;i++) { clock_us+=5000; sample_timer_cb(NULL); }
  check_manual_shutdown_gestures();
}
int main(void) {
  const uint32_t expected[]={3,7,35,13,32,6};
  // Boot-held recovery keys must be visible without synthesizing PRESS.
  pressed=(UINT64_C(1)<<32)|(UINT64_C(1)<<6);
  assert(input_manager_init()==ESP_OK);
  assert(input_is_down(INPUT_BTN_OK) && input_is_down(INPUT_BTN_BACK) && tail==0);
  uint64_t mask=0;
  for(unsigned i=0;i<6;i++) { assert(s_button_gpio[i]==expected[i]); mask|=UINT64_C(1)<<expected[i]; }
  assert(configured==mask && wake==mask);
  settle(0);
  for(unsigned i=0;i<6;i++) {
    head=tail=0; settle(UINT64_C(1)<<expected[i]);
    assert(input_is_down(i) && input_consume_press(i) && !input_consume_press(i));
    assert(tail==1 && queue[0].button==i && queue[0].action==INPUT_ACTION_PRESS);
    settle(0); assert(!input_is_down(i) && tail==2 && queue[1].action==INPUT_ACTION_RELEASE);
  }
  settle((UINT64_C(1)<<32)|(UINT64_C(1)<<7));
  clock_us+=6000000; check_manual_shutdown_gestures(); assert(dialogs==0);
  settle((UINT64_C(1)<<32)|(UINT64_C(1)<<6));
  clock_us+=6000000; check_manual_shutdown_gestures(); assert(dialogs==0);
  settle((UINT64_C(1)<<32)|(UINT64_C(1)<<35));
  clock_us+=6000000; check_manual_shutdown_gestures(); assert(dialogs==0);
  settle(UINT64_C(1)<<6);
  clock_us+=5000000; check_manual_shutdown_gestures(); assert(dialogs==1);
  check_manual_shutdown_gestures(); assert(dialogs==1);
  return 0;
}
