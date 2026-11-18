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

"""Exercise the production capture transaction with a deferred graphics task."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / 'firmware_p4/components/Service/console/commands/cmd_screen.c').read_text()


def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


code = source[:source.index('#include')]
code += r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SHOT_SETTLE_MS 1
#define SHOT_SLOW_BAUD 460800
#define SHOT_STANDARD_BAUD 921600
#define SHOT_MAX_BAUD 2000000
#define SHOT_MAX_ROWS 320
#define SHOT_MAX_WAIT_MS 60
#define SHOT_POLL_MS 20
#define SHOT_DRAIN_MS 100
#define SHOT_FAST_GUARD_MS 40
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#define CONFIG_ESP_CONSOLE_UART_BAUDRATE 115200
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LV_RESULT_OK 0
#define SCREEN_NONE 0
#define SCREEN_COUNT 200
#define ESP_LOGE(tag,...) ((void)(tag))
#define pdMS_TO_TICKS(n) (n)
typedef int lv_display_t;
typedef int screen_id_t;
typedef void (*strip_cb_t)(int32_t,int32_t,int32_t,int32_t,const uint8_t*,int32_t);
typedef struct {int count;int ival[1];} arg_t;
static arg_t id,baud,delta;
static struct {arg_t *id,*baud,*delta;void *end;} shot_args={&id,&baud,&delta,NULL};
static const char *TAG="TEST";
static uint8_t s_cov[SHOT_MAX_ROWS],*s_frame,*s_previous;
static int s_width,s_height,s_previous_width,s_previous_height;
static bool s_capture_done,locked,graphics_task,queue_failure,stall,lock_failure,fail_after_render;
static int allocations,frees,cancellations,renders;
static int64_t clock_us;
static void (*queued)(void*);
static strip_cb_t sink;
static int display;
static int arg_parse(int c,char **v,void **a){(void)c;(void)v;(void)a;return 0;}
static void arg_print_errors(void *a,void *b,const char *c){(void)a;(void)b;(void)c;}
static bool ui_acquire(void){if(lock_failure)return false;assert(!locked);locked=true;return true;}
static void ui_release(void){assert(locked);locked=false;}
static void ui_switch_screen(int n){(void)n;}
static void *lv_screen_active(void){return &display;}
static lv_display_t *lv_display_get_default(void){return &display;}
static int lv_display_get_horizontal_resolution(void *d){(void)d;return 2;}
static int lv_display_get_vertical_resolution(void *d){(void)d;return 2;}
static void lv_obj_invalidate(void *d){(void)d;assert(locked && graphics_task);}
static void lvgl_glue_capture_begin(strip_cb_t fn){assert(locked && graphics_task);sink=fn;}
static void lvgl_glue_capture_end(void){assert(locked);sink=NULL;}
static int lv_async_call(void (*fn)(void*),void *u){assert(locked && !graphics_task && !u);if(queue_failure)return 1;queued=fn;return 0;}
static int lv_async_call_cancel(void (*fn)(void*),void *u){assert(locked && !u);if(queued==fn){queued=NULL;++cancellations;}return 0;}
static void lv_refr_now(void *d){
 (void)d;assert(locked && graphics_task && sink);++renders;
 uint8_t pixels[]={1,2,3,4,5,6,7,8};sink(0,0,1,1,pixels,4);
 if(fail_after_render)lock_failure=true;
}
static void *heap_caps_calloc(size_t n,size_t z,int caps){assert(caps==3);++allocations;return calloc(n,z);}
static void heap_caps_free(void *p){if(p){++frees;free(p);}}
static int64_t esp_timer_get_time(void){return clock_us;}
static void vTaskDelay(int ms){
 clock_us+=(int64_t)ms*1000;
 if(queued && !stall){
  void (*fn)(void*)=queued;queued=NULL;assert(!locked);locked=true;graphics_task=true;
  fn(NULL);graphics_task=false;locked=false;
 }
}
static void uart_wait_tx_done(int n,int t){(void)n;(void)t;}
static void uart_set_baudrate(int n,int b){(void)n;(void)b;}
'''
code += function('static void strip_sink(') + '\n' + function('static void capture_refresh(') + '\n'
command = function('static int cmd_screenshot(')
# Execute the unmodified allocation/render/ownership portion. Serial encoding is
# outside this transaction and is exercised by real-device capture acceptance.
code += command[:command.index('  const bool is_rle')] + '  result = 0;\n' + command[command.index('cleanup:'):]
code += r'''
int main(void){
 assert(cmd_screenshot(0,NULL)==0);assert(renders==1 && allocations==frees && !sink && !queued && !s_frame);
 queue_failure=true;assert(cmd_screenshot(0,NULL)==1);assert(allocations==frees && !s_frame);
 queue_failure=false;stall=true;assert(cmd_screenshot(0,NULL)==1);assert(cancellations==1 && allocations==frees && !queued);
 stall=false;fail_after_render=true;assert(cmd_screenshot(0,NULL)==1);assert(s_frame && allocations==frees+1 && !sink);
 lock_failure=false;fail_after_render=false;assert(cmd_screenshot(0,NULL)==0);assert(allocations==frees && !s_frame && !queued);
 puts("PASS: graphics-task capture, coherent sink lifetime, queue failure, timeout cancellation and retained-buffer recovery");
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
