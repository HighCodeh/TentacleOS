#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Exercise production CDC restart/cleanup with mocked USB and task APIs."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "firmware_p4/components/Service/host_link/host_link_cdc.c").read_text()
code = source[source.index("static esp_err_t init_locked(void)"):]
notice = (ROOT / "tools/highboy/tests/usb_role_test.c").read_text().split("#include")[0]
fixture = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define HOST_LINK_CDC_ITF 0
#define HOST_LINK_CDC_TASK_STK 2048
#define SYS_PRIO_SERVICE_HI 5
#define SYS_CORE_RADIO 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
static const char *TAG="CDC_TEST";
static void *s_worker;
static bool s_cdc_up;
static atomic_bool s_stop, s_worker_done;
static bool native=true, host, held, fail_task, fail_acm;
static unsigned installs, deinits, tasks, releases;
static int (*registered_cleanup)(void);
static void cdc_rx_cb(void) {}
static void cdc_line_state_cb(void) {}
static void cdc_write(void) {}
static void host_link_worker(void *arg) {(void)arg;}
static int cleanup(void);
typedef struct { int cdc_port; void (*callback_rx)(void); void (*callback_line_state_changed)(void); } tinyusb_config_cdcacm_t;
static bool busb_msc_is_exposed(void) {return false;}
static bool usb_mux_is_native(void) {return native;}
static bool busb_host_owned(void) {return host;}
static int busb_init(void) {return host?ESP_ERR_INVALID_STATE:ESP_OK;}
static bool busb_device_acquire(void) {assert(!held); if(host)return false;held=true;return true;}
static void busb_device_release(void) {assert(held);held=false;}
static void busb_set_device_cleanup_cb(int (*cb)(void)) {assert(held);registered_cleanup=cb;}
static int host_link_start_worker(void) {return ESP_OK;}
static void host_link_session_release(void (*writer)(void)) {assert(writer==cdc_write);releases++;}
static int tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *cfg) {
  assert(held && cfg->callback_rx==cdc_rx_cb && cfg->callback_line_state_changed==cdc_line_state_cb);
  if(fail_acm)return ESP_FAIL;installs++;return ESP_OK;
}
static int tinyusb_cdcacm_deinit(int itf) {assert(held && itf==0);deinits++;return ESP_OK;}
static int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,int size,void *arg,int prio,void **out,int core) {
  assert(fn==host_link_worker && name && size==2048 && !arg && prio==5 && core==0);
  if(fail_task)return 0;tasks++;*out=(void *)1;return pdPASS;
}
static void vTaskDelay(unsigned ticks) {assert(ticks==10 && atomic_load(&s_stop));atomic_store(&s_worker_done,true);}
/* IMPLEMENTATION */
int main(void) {
  for(unsigned i=0;i<100;i++) {
    assert(host_link_cdc_init()==ESP_OK && s_cdc_up && s_worker);
    assert(host_link_cdc_init()==ESP_OK && installs==i+1 && tasks==i+1);
    assert(registered_cleanup && !held);held=true;
    assert(registered_cleanup()==ESP_OK && !s_cdc_up && !s_worker);held=false;
    host=true;assert(host_link_cdc_init()==ESP_ERR_INVALID_STATE);host=false;
  }
  assert(installs==100 && deinits==100 && releases==100);
  fail_task=true;assert(host_link_cdc_init()==ESP_FAIL && !s_cdc_up);fail_task=false;
  fail_acm=true;assert(host_link_cdc_init()==ESP_FAIL && !s_cdc_up);fail_acm=false;
  assert(host_link_cdc_init()==ESP_OK && s_cdc_up);
  held=true;assert(cleanup()==ESP_OK);held=false;
  native=false;unsigned previous=installs;
  assert(host_link_cdc_init()==ESP_OK && installs==previous && !held);
  puts("PASS: CDC repeated init/cleanup, callback registration, host exclusion and allocation failure retry");
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-cdc-role-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(notice + fixture.replace("/* IMPLEMENTATION */", code))
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=30)
