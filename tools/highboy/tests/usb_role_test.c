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
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

typedef int esp_err_t;
typedef unsigned TickType_t;
typedef int StaticSemaphore_t;
typedef int *SemaphoreHandle_t;
typedef int portMUX_TYPE;
typedef esp_err_t (*busb_device_cleanup_cb_t)(void);
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define ESP_FAIL 3
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_LOGE(tag, ...) ((void)(tag))
static const char *TAG = "USB_TEST";
static bool s_hid_exposed, s_msc_exposed, voice, busy, stall, uninstall_fail, cleanup_fail;
static bool old_barrier_only;
static void (*late_callback)(void *);
static void *late_arg;
static unsigned depth, uninstalls, cleanups, connects, suspends, delays;
typedef struct { unsigned vbvalidoven, vbvalidovval, bvalidoven, bvalidovval; } usb_dwc_gotgctl_reg_t;
static struct { usb_dwc_gotgctl_reg_t gotgctl_reg; } USB_DWC_HS;
static bool xPortInIsrContext(void) { return false; }
static void *xSemaphoreCreateRecursiveMutexStatic(void *storage) { return storage; }
static int xSemaphoreTakeRecursive(void *mutex, unsigned ticks) {
  (void)ticks; assert(mutex); if(busy) return 0; ++depth; return pdTRUE;
}
static void xSemaphoreGiveRecursive(void *mutex) { assert(mutex && depth); --depth; }
static void vTaskDelay(unsigned ticks) { delays += ticks; }
static bool busb_voice_is_exposed(void) { return voice; }
static void tud_disconnect(void) { assert(depth); }
static void tud_connect(void) { ++connects; }
static void tud_umount_cb(void) { ++suspends; }
static void usbd_defer_func(void (*cb)(void *), void *arg, bool isr) {
  assert(!isr && depth);
  if (old_barrier_only) { assert(late_callback); late_callback(late_arg); }
  else if (!stall) cb(arg);
  else { late_callback=cb; late_arg=arg; }
}
static int tinyusb_driver_uninstall(void) { ++uninstalls; return uninstall_fail ? ESP_FAIL : ESP_OK; }
bool busb_host_owned(void);
esp_err_t busb_device_quiesce(void);
/* ROLE_IMPLEMENTATION */
static esp_err_t cleanup(void) { ++cleanups; assert(!s_installed && depth); return cleanup_fail?ESP_FAIL:ESP_OK; }
static void install_device(void) {
  assert(!busb_host_owned() && !s_teardown_failed);
  s_installed=true; atomic_store(&s_phy_owner,USB_OWNER_DEVICE);
  assert(busb_device_acquire()); busb_set_device_cleanup_cb(cleanup); busb_device_release();
}
int main(void) {
  assert(busb_host_claim()); assert(!busb_device_acquire()); busb_host_release();
  for(unsigned i=0;i<100;i++) {
    install_device();
    assert(busb_host_claim() && busb_host_owned() && !s_installed && depth==0);
    assert(!busb_host_claim() && !busb_device_acquire());
    busb_host_release(); assert(!busb_host_owned());
  }
  assert(uninstalls==100 && cleanups==100 && suspends==100);
  install_device();
  s_msc_exposed=true;assert(!busb_host_claim());s_msc_exposed=false;
  s_hid_exposed=true;assert(!busb_host_claim());s_hid_exposed=false;
  voice=true;assert(!busb_host_claim());voice=false;
  busy=true;assert(!busb_host_claim() && !busb_device_acquire());busy=false;
  stall=true;assert(!busb_host_claim() && s_installed && connects==1);stall=false;
  assert(delays==USB_ROLE_WAIT_MS && uninstalls==100);
  old_barrier_only=true;assert(!busb_host_claim() && s_installed);old_barrier_only=false;
  cleanup_fail=true;assert(!busb_host_claim() && !s_installed && !busb_host_owned());
  cleanup_fail=false;assert(busb_host_claim());busb_host_release();
  install_device();uninstall_fail=true;
  assert(!busb_host_claim() && s_teardown_failed && !busb_device_acquire());
  unsigned attempts=uninstalls;
  assert(!busb_host_claim() && uninstalls==attempts && depth==0);
  puts("PASS: USB role cycles, active sessions, I/O contention, drain timeout and partial teardown isolation");
}
