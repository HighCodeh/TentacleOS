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

from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
src=(ROOT / 'firmware_p4/components/Applications/ui/screens/files/usb_storage_ui.c').read_text()
a=src.index('static void usb_storage_input('); b=src.index('\nvoid ui_usb_storage_open',a)
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct { int action, button; } input_event_t;
enum {INPUT_ACTION_PRESS, INPUT_ACTION_RELEASE};
enum {INPUT_BTN_BACK, INPUT_BTN_OK, INPUT_BTN_LEFT};
enum {USB_MSC_ACTIVE, USB_MSC_ERROR, USB_MSC_IDLE, USB_MSC_ENTERING, USB_MSC_EXITING, USB_MSC_SD_UNAVAILABLE};
enum {SCREEN_HOME, SCREEN_FILES, UI_FB_SELECT, USB_MSC_TASK_STACK, SYS_PRIO_SERVICE_HI, SYS_CORE_RADIO};
static bool s_confirm_exit, s_exit_requested, connected, s_task_failed, fail_task;
#define pdPASS 1
#define ESP_LOGE(tag, ...) ((void)0)
static int state=USB_MSC_ACTIVE, workers, switches, last_screen;
static struct {int text_secondary;} current_theme;
static int usb_msc_get_state(void) {return state;}
static bool usb_msc_host_connected(void) {return connected;}
static void set_texts(const char *a,int b,const char *c) {(void)a;(void)b;(void)c;}
static void ui_feedback(int a) {(void)a;}
static void exit_task(void *a) {(void)a;}
static void ui_switch_screen(int a) {last_screen=a; switches++;}
static int xTaskCreatePinnedToCore(void (*a)(void*),const char*b,int c,void*d,int e,void*f,int g) {
(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; if(fail_task) return 0; workers++; return pdPASS;
}
'''
main=r'''
int main(void) {
input_event_t back={INPUT_ACTION_PRESS,INPUT_BTN_BACK}, ok={INPUT_ACTION_PRESS,INPUT_BTN_OK};
connected=true;
usb_storage_input(&ok,NULL); assert(workers==0);
usb_storage_input(&back,NULL); assert(s_confirm_exit && workers==0);
usb_storage_input(&back,NULL); assert(!s_confirm_exit && workers==0);
usb_storage_input(&back,NULL); usb_storage_input(&ok,NULL);
assert(workers==1 && s_exit_requested && !s_confirm_exit);
usb_storage_input(&ok,NULL); assert(workers==1);
s_exit_requested=false; connected=false; fail_task=true;
usb_storage_input(&back,NULL); assert(workers==1 && s_task_failed && !s_exit_requested);
fail_task=false; usb_storage_input(&back,NULL); assert(workers==2 && !s_task_failed);
s_exit_requested=false; state=USB_MSC_ERROR; usb_storage_input(&back,NULL); assert(workers==3);
state=USB_MSC_ENTERING; usb_storage_input(&back,NULL); assert(workers==3);
state=USB_MSC_IDLE; usb_storage_input(&back,NULL); assert(switches==1);
state=USB_MSC_SD_UNAVAILABLE; s_exit_requested=true;
usb_storage_input(&back,NULL); assert(switches==2 && last_screen==SCREEN_HOME && workers==3);
}
'''
with tempfile.TemporaryDirectory(prefix="hb-usb-ui-") as temp:
    work=Path(temp)
    license_text=(ROOT / "firmware_p4/components/Service/fatfs/patch_manifest.json.license").read_text()
    c_notice="\n".join("// " + line for line in license_text.splitlines()) + "\n"
    (work / "test.c").write_text(c_notice+harness+src[a:b]+main)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(work / 'test.c'),'-o',str(work / 'test')],check=True)
    subprocess.run([str(work / 'test')],check=True)
print('PASS: actual USB Storage input handler: confirm, cancel, worker allocation failure, retry, transitions')
