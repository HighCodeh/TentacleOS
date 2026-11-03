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
#define TAG CONFIRM_TAG
#include "../../firmware_p4/components/Drivers/pins/board_control.c"
#undef TAG
#include "../../firmware_p4/components/Drivers/tusb_desc/usb_mux.c"

int critical, is_isr, task_fail, locked;
static int64_t now;
static board_control_status_t remote;
static int events[64], count, fail_event, busb_fail;
static bool stall, reboot_during_delay;
static unsigned select_level;

int64_t esp_timer_get_time(void) { return now; }
void vTaskDelay(int ticks) {
  now += ticks * 1000;
  if (reboot_during_delay) { remote.epoch++; reboot_during_delay=false; }
}
static int event(int code) { events[count++]=code; return count==fail_event ? ESP_FAIL : ESP_OK; }
int busb_init(void) { return busb_fail ? ESP_FAIL : ESP_OK; }
esp_err_t gpio_set_level(int pin, uint32_t value) {
  assert(pin == GPIO_USB_MUX_SEL_PIN);
  if (value) assert(!remote.outputs[BOARD_CONTROL_USB].confirmed);
  int ret=event(value ? 2 : 3);
  if(ret==ESP_OK) select_level=value;
  return ret;
}
static int status(board_control_status_t *out) { *out=remote; return ESP_OK; }
static int enable(bool value) {
  int ret=event(value ? 4 : 1);
  if(ret!=ESP_OK) return ret;
  remote.outputs[BOARD_CONTROL_USB].is_pending=stall;
  if(!stall) remote.outputs[BOARD_CONTROL_USB].confirmed=value;
  return ESP_OK;
}
static const board_control_ops_t ops={.request_usb_enable=enable,.get_status=status};
static void reset(void) {
  remote=(board_control_status_t){.is_compatible=true,.epoch=9,.capabilities=1U<<BOARD_CONTROL_USB};
  remote.outputs[BOARD_CONTROL_USB].is_known=true;
  remote.outputs[BOARD_CONTROL_USB].confirmed=true;
  count=0; fail_event=0; busb_fail=0; stall=false;
  atomic_store(&s_native,false); s_restore=false; s_pending=false;
  select_level=0;
}
int main(void) {
  reset(); usb_mux_bind_controls(&ops); usb_mux_init(); poll_once(); reset();
  assert(usb_mux_request_native(true)==ESP_OK && count==0);
  poll_once();
  assert(usb_mux_result()==ESP_OK && usb_mux_is_native());
  assert(count==3 && events[0]==1 && events[1]==2 && events[2]==4);
  remote.epoch++;
  assert(!usb_mux_is_native()); poll_once(); assert(select_level==0);
  reset(); busb_fail=1;
  assert(usb_mux_set_native(true)==ESP_FAIL && count==0);
  reset(); fail_event=2;
  assert(usb_mux_set_native(true)==ESP_FAIL && s_restore && !usb_mux_is_native());
  fail_event=0; poll_once(); assert(select_level==0 && remote.outputs[BOARD_CONTROL_USB].confirmed);
  reset(); fail_event=3;
  assert(usb_mux_set_native(true)==ESP_FAIL && s_restore);
  fail_event=0; poll_once(); assert(select_level==0 && remote.outputs[BOARD_CONTROL_USB].confirmed);
  reset(); reboot_during_delay=true;
  assert(usb_mux_set_native(true)==ESP_ERR_INVALID_STATE && select_level==0);
  reset(); stall=true;
  assert(usb_mux_set_native(true)==ESP_ERR_TIMEOUT && s_restore && select_level==0);
  // A late disconnect is repaired after its ACK becomes observable.
  stall=false; remote.outputs[BOARD_CONTROL_USB].is_pending=false;
  remote.outputs[BOARD_CONTROL_USB].confirmed=false;
  poll_once(); assert(remote.outputs[BOARD_CONTROL_USB].confirmed && !s_restore);
  reset(); locked=1;
  assert(usb_mux_request_native(false)==ESP_OK); locked=0;
  poll_once(); assert(select_level==0 && usb_mux_result()==ESP_OK);
  reset(); remote.is_compatible=false; remote.link_result=ESP_ERR_NOT_SUPPORTED;
  assert(usb_mux_set_native(true)==ESP_ERR_NOT_SUPPORTED);
  reset(); remote.capabilities=0;
  assert(usb_mux_set_native(true)==ESP_ERR_NOT_SUPPORTED);
  reset(); is_isr=1; assert(usb_mux_set_native(true)==ESP_ERR_INVALID_STATE); is_isr=0;
  return 0;
}
