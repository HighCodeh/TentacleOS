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
#define SHOT_B64_BUF 1400
#define SHOT_RLE_RUN_MAX 255
#define SHOT_RLE_BYTES 3
#define SHOT_ROW_YIELD_TICKS 1
#define ESP_OK 0
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
static arg_t id,baud,delta,rle,goto_id;
static struct {arg_t *id,*baud,*delta,*rle;void *end;} shot_args={&id,&baud,&delta,&rle,NULL};
static struct {arg_t *id;void *end;} goto_args={&goto_id,NULL};
typedef struct {char b64[SHOT_B64_BUF];uint8_t packed[SHOT_MAX_ROWS*SHOT_RLE_BYTES];} shot_buffers_t;
static const char *TAG="TEST";
static uint8_t s_cov[SHOT_MAX_ROWS],*s_frame,*s_previous;
static int s_width,s_height,s_previous_width,s_previous_height;
static bool s_capture_done,locked,graphics_task,queue_failure,stall,lock_failure,fail_after_render;
static int allocations,frees,cancellations,renders,attempts,fail_alloc_attempt;
static int width=2,height=2,switched,encoded_rows,current_baud=CONFIG_ESP_CONSOLE_UART_BAUDRATE;
static size_t largest_encoded;
static uint32_t s_sequence;
static bool encode_failure;
static int64_t clock_us;
static void (*queued)(void*);
static void *queued_context;
static strip_cb_t sink;
static int display;
static int arg_parse(int c,char **v,void **a){(void)c;(void)v;(void)a;return 0;}
static void arg_print_errors(void *a,void *b,const char *c){(void)a;(void)b;(void)c;}
static bool ui_acquire(void){if(lock_failure)return false;assert(!locked);locked=true;return true;}
static void ui_release(void){assert(locked);locked=false;}
static void ui_switch_screen(int n){assert(locked && graphics_task);switched=n;}
static void *lv_screen_active(void){return &display;}
static lv_display_t *lv_display_get_default(void){return &display;}
static int lv_display_get_horizontal_resolution(void *d){(void)d;return width;}
static int lv_display_get_vertical_resolution(void *d){(void)d;return height;}
static void lv_obj_invalidate(void *d){(void)d;assert(locked && graphics_task);}
static void lvgl_glue_capture_begin(strip_cb_t fn){assert(locked && graphics_task);sink=fn;}
static void lvgl_glue_capture_end(void){assert(locked);sink=NULL;}
static int lv_async_call(void (*fn)(void*),void *u){assert(locked && !graphics_task);if(queue_failure)return 1;queued=fn;queued_context=u;return 0;}
static int lv_async_call_cancel(void (*fn)(void*),void *u){assert(locked);if(queued==fn && queued_context==u){queued=NULL;++cancellations;}return 0;}
static void lv_refr_now(void *d){
 (void)d;assert(locked && graphics_task && sink);++renders;
 uint8_t pixels[SHOT_MAX_ROWS*2];
 for(int x=0;x<width;x++){pixels[2*x]=(uint8_t)x;pixels[2*x+1]=7;}
 for(int y=0;y<height;y++)sink(0,y,width-1,y,pixels,width*2);
 if(fail_after_render)lock_failure=true;
}
static void *heap_caps_calloc(size_t n,size_t z,int caps){assert(caps==3);++attempts;if(attempts==fail_alloc_attempt)return NULL;++allocations;return calloc(n,z);}
static void heap_caps_free(void *p){if(p){++frees;free(p);}}
static int64_t esp_timer_get_time(void){return clock_us;}
static void vTaskDelay(int ms){
 clock_us+=(int64_t)ms*1000;
 if(queued && !stall){
  void (*fn)(void*)=queued;queued=NULL;assert(!locked);locked=true;graphics_task=true;
  fn(queued_context);graphics_task=false;locked=false;
 }
}
static int uart_wait_tx_done(int n,int t){(void)n;(void)t;return ESP_OK;}
static int uart_set_baudrate(int n,int b){(void)n;current_baud=b;return ESP_OK;}
static uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t *p,size_t n){(void)p;(void)n;return crc;}
static int mbedtls_base64_encode(unsigned char *out,size_t size,size_t *used,const unsigned char *in,size_t n){
 (void)in;assert(size >= 4*((n+2)/3)+1);++encoded_rows;
 if(n>largest_encoded)largest_encoded=n;
 if(encode_failure)return -1;
 out[0]='A';out[1]='A';*used=2;return 0;
}
'''
for signature in ('static void switch_screen_async(', 'static bool queue_screen_switch(',
                  'static int cmd_goto(', 'static void strip_sink(',
                  'static void capture_refresh(', 'static size_t encode_rle_row(',
                  'static int cmd_screenshot('):
    code += function(signature) + '\n'
code += r'''
int main(void){
 goto_id.ival[0]=0;assert(cmd_goto(0,NULL)==1 && !queued && !switched);
 goto_id.ival[0]=81;queue_failure=true;assert(cmd_goto(0,NULL)==1 && !queued && !switched);
 queue_failure=false;assert(cmd_goto(0,NULL)==0 && queued && !switched);
 vTaskDelay(1);assert(switched==81 && !queued);
 lock_failure=true;assert(cmd_goto(0,NULL)==1 && !queued);lock_failure=false;
 assert(cmd_screenshot(0,NULL)==0);assert(renders==1 && allocations==frees && !sink && !queued && !s_frame);
 queue_failure=true;assert(cmd_screenshot(0,NULL)==1);assert(allocations==frees && !s_frame);
 queue_failure=false;stall=true;assert(cmd_screenshot(0,NULL)==1);assert(cancellations==1 && allocations==frees && !queued);
 stall=false;fail_after_render=true;assert(cmd_screenshot(0,NULL)==1);assert(s_frame && allocations==frees+1 && !sink);
 lock_failure=false;fail_after_render=false;assert(cmd_screenshot(0,NULL)==0);assert(allocations==frees && !s_frame && !queued);
 fail_alloc_attempt=attempts+2;assert(cmd_screenshot(0,NULL)==1);assert(allocations==frees);
 fail_alloc_attempt=0;encode_failure=true;baud.count=1;baud.ival[0]=SHOT_STANDARD_BAUD;
 assert(cmd_screenshot(0,NULL)==1);assert(allocations==frees && current_baud==CONFIG_ESP_CONSOLE_UART_BAUDRATE);
 encode_failure=false;baud.count=0;rle.count=1;width=SHOT_MAX_ROWS;
 assert(cmd_screenshot(0,NULL)==0);assert(largest_encoded==SHOT_MAX_ROWS*SHOT_RLE_BYTES && allocations==frees);
 delta.count=1;delta.ival[0]=0;assert(cmd_screenshot(0,NULL)==0);assert(s_previous && allocations==frees+1);
 delta.ival[0]=(int)s_sequence;int before=encoded_rows;
 assert(cmd_screenshot(0,NULL)==0);assert(encoded_rows==before && allocations==frees+1);
 delta.count=0;assert(cmd_screenshot(0,NULL)==0);assert(!s_previous && allocations==frees);
 puts("PASS: deferred navigation/capture, failure cleanup, bounded encoder stack, wide RLE and retained delta frames");
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wframe-larger-than=1024',
                    '-fsanitize=undefined', str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
