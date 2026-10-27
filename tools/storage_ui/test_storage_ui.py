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

"""Run production SD dialog and formatting callbacks against explicit UI/VFS fakes."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "firmware_p4/components/Applications/ui"


def function(source, signature):
    pattern = r"\s+".join(re.escape(token) for token in signature.split())
    start = re.search(pattern, source).start()
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    header = (UI / "components/header/header_ui.c").read_text()
    storage = (UI / "screens/settings/storage_settings_ui.c").read_text()
    notice = header.split('#include', 1)[0]
    code = notice + FIXTURES
    for name in ("static void fmt_bytes(", "static bool sd_try_mount("):
        code += function(header, name)
    for name in ("static void overlay_deleted_cb(", "static void format_done_poll(",
                 "static void format_task(", "static void fmt_confirm_cb("):
        code += function(storage, name)
    code += TESTS
    with tempfile.TemporaryDirectory(prefix="storage-ui-") as directory:
        source = Path(directory) / "test.c"
        binary = Path(directory) / "test"
        source.write_text(code)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)
    print("PASS: actual filesystem label, format confirmation/failures, activity bar and deleted overlay")


FIXTURES = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef struct {int start, end;} lv_obj_t;
typedef int lv_timer_t;
typedef struct {lv_obj_t *target;} lv_event_t;
typedef struct {uint64_t total_bytes, used_bytes, free_bytes;} vfs_statvfs_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define SD_MOUNT_RETRIES 3
#define SD_MOUNT_RETRY_DELAY_MS 150
#define VFS_MOUNT_POINT "/sdcard"
#define pdMS_TO_TICKS(x) (x)
#define FMT_POLL_MS 50
#define FMT_TASK_STACK 8192
#define SYS_PRIO_SERVICE_HI 1
#define SYS_CORE_RADIO 0
#define pdPASS 1
#define FMT_BAR_MAX 100
#define FMT_BAR_SPAN 25
#define FMT_BAR_STEP 5
#define LV_ANIM_OFF 0
#define LV_MIN(a,b) ((a)<(b)?(a):(b))
#define LV_MAX(a,b) ((a)>(b)?(a):(b))
#define NOTIFY_WARNING 1
#define NOTIFY_SAVED 2
static char s_sd_name[24], s_sd_size[16], s_sd_free[16], s_sd_fmt[12];
static int s_sd_used_pct;
static bool mounted=true, present=true, timer_fail, task_fail;
static const char *filesystem="exFAT";
static lv_obj_t screen, overlay, bar;
static lv_timer_t timer_instance;
static lv_obj_t *s_screen=&screen, *s_fmt_overlay, *s_fmt_bar;
static int32_t s_fmt_phase;
static bool s_formatting, s_format_exfat;
static atomic_bool s_format_done;
static esp_err_t s_format_result, native_result;
static lv_timer_t *s_format_poll_timer;
static uint32_t s_screen_generation=1, s_format_view_generation=1;
static unsigned calls, creates, rebuilds, bar_updates;
static bool used_exfat;
static int notice_kind;
static char notice_text[64];
static bool vfs_sdcard_is_mounted(void) {return mounted;}
static bool sd_card_present(void) {return present;}
static int vfs_sdcard_init(void) {return ESP_FAIL;}
static void vTaskDelay(int ticks) {(void)ticks;}
static void vTaskDelete(void *task) {(void)task;}
static bool vfs_sdcard_get_name(char *out, size_t n) {snprintf(out,n,"Test card");return true;}
static int vfs_statvfs(const char *path, vfs_statvfs_t *out) {
  (void)path;*out=(vfs_statvfs_t){1024,256,768};return ESP_OK;
}
static const char *vfs_sdcard_fs_type(void) {return filesystem;}
static int vfs_sdcard_format_exfat(void) {calls++;used_exfat=true;return native_result;}
static int vfs_sdcard_format(void) {calls++;used_exfat=false;return native_result;}
static lv_obj_t *lv_event_get_target(lv_event_t *event) {return event->target;}
static lv_obj_t *lv_screen_active(void) {return &screen;}
static void lv_bar_set_start_value(lv_obj_t *obj,int value,int anim) {
  (void)anim;assert(obj==&bar && value>=0 && value<=obj->end);obj->start=value;bar_updates++;
}
static void lv_bar_set_value(lv_obj_t *obj,int value,int anim) {
  (void)anim;assert(obj==&bar && value>=obj->start && value<=100);obj->end=value;bar_updates++;
}
static void lv_timer_delete(lv_timer_t *timer) {assert(timer==&timer_instance);}
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *),int ms,void *arg) {
  (void)cb;(void)ms;(void)arg;return timer_fail?NULL:&timer_instance;
}
static void lv_obj_del(lv_obj_t *obj) {assert(obj==&overlay);s_fmt_bar=NULL;}
static void notify(int kind,const char *text) {notice_kind=kind;snprintf(notice_text,sizeof(notice_text),"%s",text);}
static void build_screen(void) {rebuilds++;}
static void show_fmt_overlay(void) {s_fmt_overlay=&overlay;s_fmt_bar=&bar;bar=(lv_obj_t){0,25};s_fmt_phase=25;}
static int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,int stack,void *arg,int priority,void *handle,int core) {
  (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)core;
  creates++;return task_fail?0:pdPASS;
}
"""

TESTS = r"""
int main(void) {
  const char *types[]={"exFAT","FAT32","FAT16","?"};
  for(unsigned i=0;i<sizeof(types)/sizeof(types[0]);i++) {
    filesystem=types[i];assert(sd_try_mount());assert(strcmp(s_sd_fmt,types[i])==0);
  }
  mounted=false;present=false;assert(!sd_try_mount());mounted=true;
  fmt_confirm_cb(false);assert(!creates && !s_formatting);
  timer_fail=true;fmt_confirm_cb(true);assert(!creates && !s_formatting);timer_fail=false;
  task_fail=true;fmt_confirm_cb(true);assert(!s_formatting && !s_format_poll_timer && !s_fmt_bar);task_fail=false;
  s_format_exfat=true;fmt_confirm_cb(true);assert(s_formatting && s_fmt_bar);
  unsigned previous=creates;fmt_confirm_cb(true);assert(creates==previous);
  for(unsigned i=0;i<100;i++)format_done_poll(&timer_instance);
  assert(bar_updates && !calls && !rebuilds && s_formatting);
  filesystem="exFAT";format_task((void *)(uintptr_t)true);assert(used_exfat && calls==1);
  format_done_poll(&timer_instance);assert(!s_formatting && rebuilds==1 && notice_kind==NOTIFY_SAVED);
  assert(strstr(notice_text,"exFAT"));
  native_result=ESP_FAIL;fmt_confirm_cb(true);format_task(NULL);assert(!used_exfat);
  format_done_poll(&timer_instance);assert(notice_kind==NOTIFY_WARNING && rebuilds==2);
  native_result=ESP_OK;fmt_confirm_cb(true);mounted=false;format_task(NULL);format_done_poll(&timer_instance);
  assert(notice_kind==NOTIFY_WARNING && s_format_result==ESP_ERR_INVALID_STATE);mounted=true;
  fmt_confirm_cb(true);lv_event_t event={&overlay};overlay_deleted_cb(&event);
  assert(!s_fmt_bar && !s_fmt_overlay);previous=bar_updates;format_done_poll(&timer_instance);assert(bar_updates==previous);
  s_screen=NULL;format_task((void *)(uintptr_t)true);previous=rebuilds;format_done_poll(&timer_instance);
  assert(rebuilds==previous && !s_formatting);
  return 0;
}
"""

if __name__ == "__main__":
    main()
