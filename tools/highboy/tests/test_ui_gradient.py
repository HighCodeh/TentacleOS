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

"""Exercise the production RGB565 gradient and object-owned allocation lifecycle."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "firmware_p4/components/Applications/ui/components/chrome/ui_chrome.c").read_text()
production = source[source.index("#define GRADIENT_TILE_W"):]
stubs = r"""
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
typedef struct { uint8_t blue, green, red; } lv_color_t;
typedef struct { struct { int magic, cf, w, h, stride; } header;
 size_t data_size; const uint8_t *data; } lv_image_dsc_t;
typedef struct { const lv_image_dsc_t *image; int solid; } lv_obj_t;
typedef struct { void *data; } lv_event_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NO_MEM 2
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LV_GRAD_DIR_NONE 0
#define LV_OPA_COVER 255
#define LV_IMAGE_HEADER_MAGIC 25
#define LV_COLOR_FORMAT_RGB565 18
#define LV_EVENT_DELETE 1
#define ESP_LOGE(...) ((void)0)
static bool fail_alloc, fail_event;
static int allocations, frees, cache_drops;
static void *owned;
static void (*callback)(lv_event_t *);
static void *heap_caps_calloc(size_t n, size_t s, int flags) {
 assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(fail_alloc) return NULL;
 allocations++; return calloc(n,s);
}
static void checked_free(void *p) { assert(p); frees++; free(p); }
#define free checked_free
static bool lv_color_eq(lv_color_t a, lv_color_t b) {
 return a.red==b.red && a.green==b.green && a.blue==b.blue;
}
static void lv_obj_set_style_bg_color(lv_obj_t *o, lv_color_t c, int s) {(void)c;(void)s;o->solid++;}
static void lv_obj_set_style_bg_grad_dir(lv_obj_t *o, int d, int s) {(void)o;(void)s;assert(d==0);}
static void lv_obj_set_style_bg_opa(lv_obj_t *o, int d, int s) {(void)o;(void)s;assert(d==255);}
static void lv_obj_set_style_bg_image_tiled(lv_obj_t *o, bool d, int s) {(void)o;(void)s;assert(d);}
static void lv_obj_set_style_bg_image_opa(lv_obj_t *o, int d, int s) {(void)o;(void)s;assert(d==255);}
static void lv_obj_set_style_bg_image_src(lv_obj_t *o, const lv_image_dsc_t *d, int s) {(void)s;o->image=d;}
static void *lv_obj_add_event_cb(lv_obj_t *o, void (*cb)(lv_event_t *), int code, void *p) {
 assert(o && code==LV_EVENT_DELETE); if(fail_event) return NULL;
 owned=p;callback=cb;return p;
}
static void *lv_event_get_user_data(lv_event_t *e) {return e->data;}
static void lv_image_cache_drop(const lv_image_dsc_t *p) {assert(p); cache_drops++;}
"""
tests = r"""
static double channel(uint16_t v, int c) {
 return c==0 ? (v>>11)*255.0/31 : c==1 ? ((v>>5)&63)*255.0/63 : (v&31)*255.0/31;
}
int main(void) {
 lv_obj_t o={0};lv_color_t black={0},white={255,255,255};
 assert(ui_chrome_gradient(NULL,black,white,320)==ESP_ERR_INVALID_ARG);
 assert(ui_chrome_gradient(&o,black,white,0)==ESP_ERR_INVALID_ARG);
 assert(ui_chrome_gradient(&o,black,white,321)==ESP_ERR_INVALID_ARG);
 assert(ui_chrome_gradient(&o,black,black,320)==ESP_OK && allocations==0 && o.solid==1);
 fail_alloc=true;
 assert(ui_chrome_gradient(&o,black,white,320)==ESP_ERR_NO_MEM && !o.image);
 fail_alloc=false;fail_event=true;
 assert(ui_chrome_gradient(&o,black,white,320)==ESP_ERR_NO_MEM && allocations==frees);
 fail_event=false;
 for(int h=1;h<=320;h++) {
  assert(ui_chrome_gradient(&o,black,white,h)==ESP_OK);
  assert(o.image->header.stride==64 && o.image->data_size==(size_t)h*64);
  const uint16_t *pix=(const uint16_t *)o.image->data;
  for(int x=0;x<32;x++) {assert(pix[x]==0);if(h>1)assert(pix[(h-1)*32+x]==65535);}
  lv_event_t e={owned};callback(&e);o.image=NULL;
 }
 // Dark colored and grayscale ramps, ascending and descending. Average blocks
 // must approximate the requested color without broad uniform quantization bands.
 const lv_color_t ends[][2]={{{45,19,10},{0,0,0}},{{0,0,0},{45,19,10}},{{48,48,48},{8,8,8}}};
 for(unsigned t=0;t<sizeof(ends)/sizeof(ends[0]);t++) {
  uint16_t pixels[GRADIENT_TILE_W*320];
  gradient_fill(pixels,320,ends[t][0],ends[t][1]);
  for(int y=8;y<304;y+=8) for(int c=0;c<3;c++) {
   double mean=0;for(int dy=0;dy<8;dy++)for(int x=0;x<8;x++)mean+=channel(pixels[(y+dy)*32+x],c)/64;
   const uint8_t *a=(const uint8_t *)&ends[t][0],*b=(const uint8_t *)&ends[t][1];
   double ideal=a[2-c]+(b[2-c]-a[2-c])*(y+3.5)/319;
   assert(fabs(mean-ideal)<0.75);
  }
 }
 assert(allocations==frees && cache_drops==320);
 puts("RGB565 dark-ramp accuracy, endpoints, height bounds, PSRAM failure and deletion passed");
}
"""
with tempfile.TemporaryDirectory(prefix="ui-gradient-") as tmp:
    c=Path(tmp)/"gradient.c";exe=Path(tmp)/"gradient"
    c.write_text(source.split("#include",1)[0]+stubs+production+tests)
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror",
                    "-fsanitize=undefined","-g",str(c),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
