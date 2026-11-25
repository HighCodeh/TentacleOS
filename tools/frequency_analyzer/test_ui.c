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
#include "frequency_analyzer.h"
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
#define SCREEN_SUBGHZ_MENU 102
#define SCREEN_SUBGHZ_READ 103
#define UI_FB_NAV 0
typedef struct { int button, action; } input_event_t;
ui_theme_t current_theme;
static frequency_analyzer_snapshot_t fake;
static bool running, held, raw;
static unsigned starts, stops, selected_screen;
static int start_error;
static uint32_t read_frequency;
esp_err_t frequency_analyzer_start(frequency_analyzer_mode_t mode, int16_t threshold) {
  if (start_error) return start_error;
  assert(!running); starts++; running = true; fake.mode = mode; fake.threshold_dbm = threshold;
  return ESP_OK;
}
void frequency_analyzer_stop(void) { stops++; running = false; }
bool frequency_analyzer_is_running(void) { return running; }
void frequency_analyzer_get_snapshot(frequency_analyzer_snapshot_t *out) { *out = fake; }
esp_err_t frequency_analyzer_configure(frequency_analyzer_mode_t mode, int16_t threshold) {
  fake.mode = mode; fake.threshold_dbm = threshold; return ESP_OK;
}
void frequency_analyzer_pause(bool paused) {
  fake.phase = paused ? FREQUENCY_ANALYZER_PAUSED : FREQUENCY_ANALYZER_SEARCHING;
}
void subghz_settings_set_freq(uint32_t frequency) { read_frequency = frequency; }
void ui_subghz_read_set_raw(bool is_raw) { raw = is_raw; }
static void ui_feedback(int feedback) {}
static void led_signal_info(void) {}
static void led_signal_error(void) {}
static void power_policy_set_display_hold(lv_obj_t *obj, bool hold) { held = hold; }
static int ui_screen_w(void) { return lv_display_get_horizontal_resolution(NULL); }
static int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }
void ui_subghz_frequency_stop(void);
static void ui_switch_screen(int id) { selected_screen = id; ui_subghz_frequency_stop(); }
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
static void ui_chrome_footer_set_text(lv_obj_t *obj, const char *text) { lv_label_set_text(obj,text); }
static void ui_screen_load_owned(lv_obj_t **slot, lv_obj_t *obj) { *slot = obj; lv_screen_load(obj); }
#include "subghz_frequency_ui.c"
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*b){(void)a;(void)b;lv_display_flush_ready(d);}
static void capture(const char*name){
 lv_obj_update_layout(s_screen);lv_draw_buf_t*b=lv_snapshot_take(s_screen,LV_COLOR_FORMAT_RGB565);assert(b);
 FILE*f=fopen(name,"wb");assert(f);fprintf(f,"P6\n%u %u\n255\n",b->header.w,b->header.h);
 for(unsigned y=0;y<b->header.h;y++)for(unsigned x=0;x<b->header.w;x++){
  const uint8_t*p=b->data+y*b->header.stride+x*2;uint16_t c=p[0]|p[1]<<8;
  uint8_t rgb[]={(uint8_t)(((c>>11)&31)*255/31),(uint8_t)(((c>>5)&63)*255/63),(uint8_t)((c&31)*255/31)};fwrite(rgb,1,3,f);
 }fclose(f);lv_draw_buf_destroy(b);
}


static void bounds(lv_obj_t *obj) {
  lv_area_t area; lv_obj_get_coords(obj, &area);
  assert(area.x1 >= 0 && area.x2 < ui_screen_w());
  assert(area.y1 >= UI_CHROME_HEADER_H && area.y2 < ui_screen_h()-UI_CHROME_FOOTER_H);
}
static void press(int button) { input(&(input_event_t){button, INPUT_ACTION_PRESS}, NULL); }
int main(int argc, char **argv) {
  assert(argc == 2); lv_init(); lv_display_t *display = lv_display_create(240,320);
  static uint8_t buffer[320 * 20 * 2];
  lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  for (unsigned wide = 0; wide < 2; wide++) for (unsigned light = 0; light < 2; light++) {
    lv_display_set_resolution(display, wide ? 320 : 240, wide ? 240 : 320);
    current_theme = (ui_theme_t){.screen_base=lv_color_hex(light?0xf3f4f5:0x11131a),
      .bg_secondary=lv_color_hex(light?0xffffff:0x202531),
      .text_main=lv_color_hex(light?0x111827:0xf8fafc),
      .text_secondary=lv_color_hex(light?0x475569:0xa1acc0),
      .border_accent=lv_color_hex(light?0x6540c8:0xa88aff),
      .border_interface=lv_color_hex(light?0xcbd5e1:0x475569),
      .border_inactive=lv_color_hex(light?0xcbd5e1:0x475569)};
    fake = (frequency_analyzer_snapshot_t){.phase=FREQUENCY_ANALYZER_SEARCHING, .live_dbm=-110,
      .scan_frequency_hz=315000000, .trace_count=32};
    for (unsigned i=0;i<32;i++) fake.trace[i]=-110;
    start_error=0; ui_subghz_frequency_open(); update(NULL); assert(running && held);
    lv_obj_update_layout(s_screen); bounds(s_actions[0]); bounds(s_actions[1]); bounds(s_frequency);
    bounds(s_scan); bounds(s_rssi); if (s_history) bounds(s_history);
    for (unsigned i=0;i<32;i++) bounds(s_bars[i]);
    char path[1024]; snprintf(path,sizeof(path),"%s/%u-%u-search.ppm",argv[1],wide,light); capture(path);
    press(INPUT_BTN_RIGHT); assert(fake.mode==FREQUENCY_ANALYZER_315);
    press(INPUT_BTN_DOWN); assert(fake.threshold_dbm==-94);
    fake.frequency_hz=433920000; fake.phase=FREQUENCY_ANALYZER_FOUND;
    fake.rssi_dbm=-46.5; fake.detections=1; fake.live_dbm=-46.5;
    fake.history[0]=(frequency_analyzer_hit_t){433920000,-46.5};
    fake.history[1]=(frequency_analyzer_hit_t){315000000,-57};
    for (unsigned i=0;i<32;i++) fake.trace[i]=i>14 && i<23?-46:-110;
    update(NULL); lv_obj_update_layout(s_screen);
    lv_area_t frequency_area, rssi_area;
    lv_obj_get_coords(s_frequency, &frequency_area);
    lv_obj_get_coords(s_rssi, &rssi_area);
    assert(frequency_area.y2 < rssi_area.y1);
    assert(strstr(lv_label_get_text(s_rssi), "-46.5 dBm"));
    assert(strstr(lv_label_get_text(s_scan), "dBm"));
    if (s_history) {
      const lv_font_t *font = lv_obj_get_style_text_font(s_history, 0);
      int32_t needed = FREQUENCY_ANALYZER_HISTORY * font->line_height +
          (FREQUENCY_ANALYZER_HISTORY - 1) * lv_obj_get_style_text_line_space(s_history, 0);
      assert(lv_obj_get_height(s_history) >= needed);
      lv_area_t history_area, actions_area;
      lv_obj_get_coords(s_history, &history_area);
      lv_obj_get_coords(s_actions[0], &actions_area);
      assert(history_area.y2 < actions_area.y1);
    }
    snprintf(path,sizeof(path),"%s/%u-%u-found.ppm",argv[1],wide,light); capture(path);
    press(INPUT_BTN_OK); update(NULL); assert(s_paused);
    snprintf(path,sizeof(path),"%s/%u-%u-paused.ppm",argv[1],wide,light); capture(path);
    press(INPUT_BTN_RIGHT); press(INPUT_BTN_OK); update(NULL);
    assert(raw && read_frequency==433920000 && selected_screen==SCREEN_SUBGHZ_READ && !running && !held);
    ui_subghz_frequency_stop(); lv_obj_delete(s_screen);
    assert(!s_screen && !s_timer);
    fake=(frequency_analyzer_snapshot_t){0}; start_error=ESP_ERR_NO_MEM;
    ui_subghz_frequency_open(); update(NULL); assert(s_error && !running && !held);
    snprintf(path,sizeof(path),"%s/%u-%u-error.ppm",argv[1],wide,light); capture(path);
    press(INPUT_BTN_BACK); assert(selected_screen==SCREEN_SUBGHZ_MENU); lv_obj_delete(s_screen);
  }
  assert(starts==stops);
  puts("PASS: production LVGL view, both orientations/themes, control bounds, unclipped history, RSSI numbers, RAW handoff and cleanup");
}
