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
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "frequency_waterfall.h"
#include <stdlib.h>
#include <math.h>
#include "subghz_settings.h"
#include "ui_theme.h"

#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
#define INPUT_ACTION_PRESS 1
#define INPUT_ACTION_REPEAT 2
#define INPUT_BTN_BACK 1
#define INPUT_BTN_OK 2
#define INPUT_BTN_UP 3
#define INPUT_BTN_DOWN 4
#define INPUT_BTN_LEFT 5
#define INPUT_BTN_RIGHT 6
#define SCREEN_SUBGHZ_SPECTRUM 66
#define SCREEN_SUBGHZ_WATERFALL 200
#define SCREEN_SUBGHZ_MENU 102
#define SCREEN_SUBGHZ_READ 103
#define UI_FB_NAV 0
typedef struct { int button, action; } input_event_t;
ui_theme_t current_theme;

#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static frequency_waterfall_snapshot_t fake;
static bool running, held, fail_alloc, fail_timer, delay_stop;
static unsigned selected_screen, allocations, releases, stops;
static uint32_t read_frequency;
static bool raw;
static void *heap_caps_calloc(size_t n, size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (fail_alloc) return NULL;
  ++allocations; return calloc(n, size);
}
static void release_buffer(void *memory) { if (memory) ++releases; free(memory); }
static lv_timer_t *create_timer(lv_timer_cb_t cb, uint32_t period, void *data) {
  return fail_timer ? NULL : lv_timer_create(cb, period, data);
}
esp_err_t frequency_waterfall_start(const frequency_waterfall_config_t *config) {
  assert(!running); fake = (frequency_waterfall_snapshot_t){.config=*config};
  running = true; return ESP_OK;
}
void frequency_waterfall_stop(void) { ++stops; if (!delay_stop) running = false; }
bool frequency_waterfall_is_running(void) { return running; }
void frequency_waterfall_pause(bool pause) { fake.is_paused = pause; }
void frequency_waterfall_get_snapshot(frequency_waterfall_snapshot_t *out) { *out = fake; }
void subghz_settings_set_freq(uint32_t hz) { read_frequency = hz; }
void ui_subghz_read_set_raw(bool value) { raw = value; }
static void ui_feedback(int feedback) {}
static void led_signal_error(void) {}
static void power_policy_set_display_hold(lv_obj_t *obj, bool hold) { held = hold; }
static int ui_screen_w(void) { return lv_display_get_horizontal_resolution(NULL); }
static int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }
void ui_subghz_waterfall_stop(void);
static void ui_switch_screen(int id) { selected_screen = id; ui_subghz_waterfall_stop(); }
static void ui_input_set_screen_handler(void (*fn)(const input_event_t *, void *), void *ctx) {}
static lv_obj_t *ui_chrome_screen_create(void) {
  lv_obj_t *obj = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(obj, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(obj, 0, 0); lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE); return obj;
}
static lv_obj_t *chrome(lv_obj_t *parent, const char *text, int y, int height) {
  lv_obj_t *obj = lv_label_create(parent); lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(obj, current_theme.text_main, 0);
  lv_obj_set_pos(obj, 8, y); lv_obj_set_size(obj, ui_screen_w() - 16, height); return obj;
}
static void ui_chrome_header(lv_obj_t *parent, const char *text, const char *icon) { chrome(parent, text, 10, 30); }
static lv_obj_t *ui_chrome_footer(lv_obj_t *parent, const char *text) { return chrome(parent, text, ui_screen_h()-22, 22); }
static void ui_screen_load_owned(lv_obj_t **slot, lv_obj_t *obj) { *slot = obj; lv_screen_load(obj); }
#define free release_buffer
#define lv_timer_create create_timer
#include "subghz_waterfall_ui.c"
#undef free
#undef lv_timer_create
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*b){(void)a;(void)b;lv_display_flush_ready(d);}
static void capture(const char*name){
 lv_obj_update_layout(s_screen);lv_draw_buf_t*b=lv_snapshot_take(s_screen,LV_COLOR_FORMAT_RGB565);assert(b);
 FILE*f=fopen(name,"wb");assert(f);fprintf(f,"P6\n%u %u\n255\n",b->header.w,b->header.h);
 for(unsigned y=0;y<b->header.h;y++)for(unsigned x=0;x<b->header.w;x++){
  const uint8_t*p=b->data+y*b->header.stride+x*2;uint16_t c=p[0]|p[1]<<8;
  uint8_t rgb[]={(uint8_t)(((c>>11)&31)*255/31),(uint8_t)(((c>>5)&63)*255/63),(uint8_t)((c&31)*255/31)};fwrite(rgb,1,3,f);
 }
 fclose(f);lv_draw_buf_destroy(b);
}



static void press(int button) { input(&(input_event_t){button, INPUT_ACTION_PRESS}, NULL); }
static void close_screen(lv_obj_t *blank) {
  ui_subghz_waterfall_stop(); ui_subghz_waterfall_stop();
  assert(!s_buffer && !s_canvas && !s_timer && !held && allocations == releases);
  lv_screen_load(blank); lv_obj_delete(s_screen); assert(!s_screen);
}
static void test_color_scale(void) {
  frequency_waterfall_color_t scale;
  float samples[FREQUENCY_WATERFALL_BINS];
  frequency_waterfall_color_reset(&scale);
  for (unsigned i = 0; i < FREQUENCY_WATERFALL_BINS; ++i) samples[i] = -105;
  assert(frequency_waterfall_color_update(&scale, samples, FREQUENCY_WATERFALL_BINS) == ESP_OK);
  assert(scale.ceiling_dbm - scale.floor_dbm >= 24);
  assert(frequency_waterfall_color_level(&scale, -105) < 51);
  samples[20] = -70;
  frequency_waterfall_color_reset(&scale);
  assert(frequency_waterfall_color_update(&scale, samples, FREQUENCY_WATERFALL_BINS) == ESP_OK);
  assert(frequency_waterfall_color_level(&scale, -70) > 240);
  assert(frequency_waterfall_color_level(&scale, -105) < 51);
  assert(frequency_waterfall_color_level(&scale, -200) == 0);
  assert(frequency_waterfall_color_level(&scale, 0) == 255);
  float previous = scale.ceiling_dbm;
  samples[20] = -40;
  assert(frequency_waterfall_color_update(&scale, samples, FREQUENCY_WATERFALL_BINS) == ESP_OK);
  assert(scale.ceiling_dbm > previous && scale.ceiling_dbm < -39);
  previous = scale.ceiling_dbm;
  samples[20] = -105;
  assert(frequency_waterfall_color_update(&scale, samples, FREQUENCY_WATERFALL_BINS) == ESP_OK);
  assert(scale.ceiling_dbm < previous && scale.ceiling_dbm > -84);
  frequency_waterfall_color_t valid = scale;
  samples[20] = NAN;
  assert(frequency_waterfall_color_update(&scale, samples, FREQUENCY_WATERFALL_BINS) == ESP_ERR_INVALID_ARG);
  assert(scale.floor_dbm == valid.floor_dbm && scale.ceiling_dbm == valid.ceiling_dbm);
  frequency_waterfall_color_reset(&scale);
  assert(!scale.is_ready);
}
int main(int argc, char **argv) {
  test_color_scale();
  assert(argc == 2); lv_init(); lv_display_t *display = lv_display_create(240,320);
  static uint8_t buffer[320 * 20 * 2];
  lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush); lv_obj_t *blank = lv_screen_active();
  for (unsigned wide=0;wide<2;++wide) for(unsigned light=0;light<2;++light) {
    lv_display_set_resolution(display,wide?320:240,wide?240:320);
    current_theme=(ui_theme_t){.screen_base=lv_color_hex(light?0xf3f4f5:0x11131a),
      .bg_secondary=lv_color_hex(light?0xffffff:0x202531),
      .text_main=lv_color_hex(light?0x111827:0xf8fafc),
      .text_secondary=lv_color_hex(light?0x475569:0xa1acc0),
      .border_accent=lv_color_hex(light?0x6540c8:0xa88aff)};
    ui_subghz_waterfall_set_center(433920000); ui_subghz_waterfall_open();
    assert(s_buffer && held && !running); tick(NULL); assert(running);
    for(unsigned row=0;row<100;++row) {
      fake.sequence++; fake.duration_us=240000; fake.progress=FREQUENCY_WATERFALL_BINS;
      for(unsigned i=0;i<FREQUENCY_WATERFALL_BINS;++i) fake.dbm[i]=-105+sin(i)*4+35*exp(-pow(((double)i-30-row%7)/3,2));
      tick(NULL);
    }
    assert(s_rows==64 && allocations==releases+1 && s_missed==0);
    for (unsigned i=0;i<s_rows*FREQUENCY_WATERFALL_BINS;++i)
      assert(s_buffer->pixels[i] == heat_color(s_buffer->history[i]));
    frequency_waterfall_color_t stable = s_buffer->colors;
    tick(NULL);
    assert(s_buffer->colors.floor_dbm == stable.floor_dbm && s_buffer->colors.ceiling_dbm == stable.ceiling_dbm);
    s_selected=SUBGHZ_WATERFALL_HOLD;press(INPUT_BTN_RIGHT);assert(s_max_hold);
    s_selected=SUBGHZ_WATERFALL_CURSOR;press(INPUT_BTN_RIGHT);assert(s_cursor!=FREQUENCY_WATERFALL_BINS / 2);
    lv_obj_update_layout(s_screen);
    lv_obj_t *objects[]={s_info,s_status,s_axis,s_plot,s_control};
    for(unsigned i=0;i<5;i++) {
      lv_area_t area;lv_obj_get_coords(objects[i],&area);
      assert(area.x1>=0 && area.x2<ui_screen_w());
      assert(area.y1>=UI_CHROME_HEADER_H && area.y2<ui_screen_h()-UI_CHROME_FOOTER_H);
    }
    char path[512];snprintf(path,sizeof(path),"%s/waterfall-%u-%u.ppm",argv[1],wide,light);capture(path);
    s_selected=SUBGHZ_WATERFALL_CENTER; refresh_controls();
    snprintf(path,sizeof(path),"%s/center-%u-%u.ppm",argv[1],wide,light);capture(path);
    s_selected=SUBGHZ_WATERFALL_CENTER;
    for (unsigned i=0;i<FREQUENCY_CATALOG_CARRIER_COUNT;++i) {
      press(INPUT_BTN_RIGHT);
      assert(frequency_waterfall_validate(&s_config)==ESP_OK);
      tick(NULL);
    }
    assert(s_requested_center_hz==433920000);
    ui_subghz_waterfall_set_center(315000000);
    assert(s_config.center_hz==315000000);
    s_config.span_hz=20000000;
    ui_subghz_waterfall_set_center(348000000);
    press(INPUT_BTN_RIGHT); assert(s_requested_center_hz==387000000);
    press(INPUT_BTN_LEFT); assert(s_requested_center_hz==348000000);
    press(INPUT_BTN_LEFT); assert(s_requested_center_hz==330000000);
    s_config.span_hz=2000000;
    ui_subghz_waterfall_set_center(433920000);
    unsigned before=allocations;
    s_selected=SUBGHZ_WATERFALL_BANDWIDTH;press(INPUT_BTN_RIGHT);
    assert(s_pending && s_rows==0 && !running && allocations==before);tick(NULL);assert(running);
    press(INPUT_BTN_OK);assert(s_paused && fake.is_paused);press(INPUT_BTN_OK);assert(!s_paused);
    fake.sequence=1;tick(NULL);
    s_selected=SUBGHZ_WATERFALL_RAW;delay_stop=true;press(INPUT_BTN_OK);tick(NULL);
    assert(s_raw && running);running=false;delay_stop=false;tick(NULL);
    assert(selected_screen==SCREEN_SUBGHZ_READ && raw && read_frequency!=0 && !s_buffer);
    close_screen(blank);
    fail_alloc=true;ui_subghz_waterfall_open();assert(s_error && !held && !running && !s_buffer);
    press(INPUT_BTN_BACK);assert(selected_screen==SCREEN_SUBGHZ_SPECTRUM);close_screen(blank);fail_alloc=false;
    fail_timer=true;ui_subghz_waterfall_open();assert(s_error && !s_buffer && !held);close_screen(blank);fail_timer=false;
    ui_subghz_waterfall_open();tick(NULL);fake.error=ESP_FAIL;tick(NULL);
    assert(s_error && !held && !running);close_screen(blank);
    ui_subghz_waterfall_open();tick(NULL);
    lv_screen_load(blank);lv_obj_delete(s_screen);
    assert(!s_screen && !s_buffer && !running && !held && allocations==releases);
    ui_subghz_waterfall_open();tick(NULL);
    current_theme.border_accent=lv_color_hex(0x123456);tick(NULL);
    assert(selected_screen==SCREEN_SUBGHZ_WATERFALL && !s_buffer && !held);close_screen(blank);
  }
  puts("PASS: bounded PSRAM history, repeated open/close, OOM/timer/error cleanup, control resets, RAW handoff, portrait/landscape and themes");
}
