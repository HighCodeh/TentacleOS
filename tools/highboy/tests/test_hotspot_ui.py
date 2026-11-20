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

"""Compile hotspot worker/close production functions with deterministic service failures."""
from pathlib import Path
import tempfile, subprocess
root=Path(__file__).resolve().parents[3]
source=(root/'firmware_p4/components/Applications/ui/screens/wifi/wifi_hotspot_ui.c').read_text()
def function(signature):
 start=source.index(signature);brace=source.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define pdPASS 1
#define AP_NAME "HighBoy_AP"
#define AP_IP "192.168.4.1"
#define AP_MAX_CONNECTIONS 8
#define TASK_STACK_SIZE 4096
#define SYS_PRIO_SERVICE_LO 2
#define SYS_CORE_RADIO 0
#define MALLOC_CAP_SPIRAM 2
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static int s_lock,locked,deleted,starts,stops,errors;
static bool s_requested,s_busy,s_online;
static esp_err_t s_error;
static void *s_timer;
static void (*pending)(void *);
static bool fail_create,cancel_during_start,fail_stop;
static void request(bool enabled);
#define portENTER_CRITICAL(lock) do {assert(!locked);locked=1;}while(0)
#define portEXIT_CRITICAL(lock) do {assert(locked);locked=0;}while(0)
static void led_signal_info(void) { assert(!locked); }
static void led_signal_error(void) { assert(!locked);errors++; }
static int wifi_service_save_ap_config(const char *s,const char *p,int n,const char *ip,bool e) {
 (void)s;(void)p;(void)ip;assert(!locked&&e&&n==8);starts++;
 if(cancel_during_start) request(false);
 return ESP_OK;
}
static int wifi_service_set_enabled(bool e) {assert(!locked&&!e);stops++;return fail_stop?2:0;}
static int xTaskCreatePinnedToCoreWithCaps(void (*fn)(void *),const char *name,int stack,void *arg,int prio,void *handle,int core,int caps) {
 (void)name;(void)arg;(void)prio;(void)handle;assert(!locked&&stack==4096&&core==0&&caps==2);
 if(fail_create) return 0;
 assert(!pending);pending=fn;return pdPASS;
}
static void vTaskDeleteWithCaps(void *task) {(void)task;assert(!locked);deleted++;}
static void lv_timer_delete(void *timer) {assert(timer==(void*)1);}
"""
code+='\n'.join(function(s) for s in ('static void apply_task(', 'static void request(', 'void ui_wifi_hotspot_stop('))
code+=r"""
static void run(void) { void (*fn)(void*)=pending;assert(fn);pending=NULL;fn(NULL); }
int main(void) {
 ui_wifi_hotspot_stop();assert(!pending&&!starts&&!stops);
 fail_create=true;request(true);assert(!s_busy&&s_error==ESP_ERR_NO_MEM&&errors==1);
 fail_create=false;request(true);assert(s_busy);request(true);run();assert(s_online&&!s_busy&&starts==1);
 s_timer=(void*)1;ui_wifi_hotspot_stop();assert(!s_timer);run();assert(!s_online&&stops==1);
 ui_wifi_hotspot_stop();assert(!pending);
 cancel_during_start=true;request(true);run();assert(!s_online&&!s_requested&&!s_busy&&starts==2&&stops==2);
 cancel_during_start=false;request(true);run();assert(s_online);
 fail_stop=true;ui_wifi_hotspot_stop();run();assert(s_online&&s_error==2&&!s_busy);
 fail_stop=false;ui_wifi_hotspot_stop();run();assert(!s_online&&!s_busy&&s_error==0);
 puts("PASS: hotspot task failure, repeated requests, close during start, stop failure/retry, idempotent close and peripheral calls outside locks");
}
"""
with tempfile.TemporaryDirectory(prefix='highboy-hotspot-') as directory:
 test=Path(directory)/'test.c';test.write_text(code);binary=Path(directory)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-variable','-fsanitize=undefined',str(test),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
