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

#include "wifi_channel_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "st7789.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "WIFI_CHAN_UI";

#define NAV_TIMER_MS     50
#define MAX_ROWS         12
#define SCAN_SIM_STEPS   4
#define SCAN_SIM_STEP_MS 220
#define TASK_STACK_SIZE 8192
#define TASK_PRIORITY SYS_PRIO_SERVICE_LO

#define COLOR_QUIET_HEX   0x00E676
#define COLOR_BUSY_HEX    0xFFC107
#define COLOR_CROWDED_HEX 0xF44336
#define COLOR_DIM_HEX     0x8A8594

#define OCCUPANCY_FULL_APS 6

#define SCAN_WAVES_Y_OFS   -6
#define SCAN_CAPTION_Y_OFS 78

#define SPEC_PANEL_TOP     (UI_CHROME_HEADER_H + 8)
#define SPEC_PANEL_MARGIN  8
#define SPEC_PANEL_W       (LCD_H_RES - SPEC_PANEL_MARGIN * 2)
#define SPEC_PANEL_H       210
#define SPEC_PANEL_PAD     8
#define SPEC_PANEL_RADIUS  10
#define SPEC_PANEL_BG_HEX  0x0A0614
#define SPEC_INNER_W       (SPEC_PANEL_W - SPEC_PANEL_PAD * 2)
#define SPEC_INNER_H       (SPEC_PANEL_H - SPEC_PANEL_PAD * 2)
#define SPEC_BASELINE_Y    (SPEC_INNER_H - 22)
#define SPEC_HUMP_MAX_H    (SPEC_BASELINE_Y - 6)
#define SPEC_CHAN_SPAN     12
#define SPEC_HUMP_HALF_NUM 5
#define SPEC_HUMP_HALF_DEN 24
#define SPEC_HUMP_PTS      15
#define SPEC_LINE_W        2
#define SPEC_LABEL_W       30
#define SPEC_LABEL_Y_OFS   4
#define SPEC_GLOW_W        14
#define SPEC_HINT_Y        266

#define REC_CANDIDATE_SPAN 2

typedef enum { SCAN_RUNNING, SCAN_DONE, SCAN_FAIL } scan_state_t;

typedef struct {
  uint8_t channel;
  uint8_t ap_count;
  int8_t best_rssi;
} mock_channel_t;

static const mock_channel_t MOCK_CHANNELS[] = {
    {1, 5, -42},
    {6, 3, -55},
    {11, 4, -49},
    {3, 1, -71},
    {9, 2, -63},
};
#define MOCK_CHANNEL_COUNT (sizeof(MOCK_CHANNELS) / sizeof(MOCK_CHANNELS[0]))

static const uint8_t REC_CANDIDATES[] = {1, 6, 11};
#define REC_CANDIDATE_COUNT (sizeof(REC_CANDIDATES) / sizeof(REC_CANDIDATES[0]))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;
static int s_row_count = 0;
static uint8_t s_row_channel[MAX_ROWS];
static uint8_t s_row_ap_count[MAX_ROWS];
static int8_t s_row_rssi[MAX_ROWS];
static uint32_t s_row_color[MAX_ROWS];

static lv_point_precise_t s_hump_pts[MAX_ROWS][SPEC_HUMP_PTS];

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *t);

static uint32_t color_for_count(int count) {
  if (count <= 2)
    return COLOR_QUIET_HEX;
  if (count <= 4)
    return COLOR_BUSY_HEX;
  return COLOR_CROWDED_HEX;
}

static int channel_center_x(int channel) {
  int cx = (channel - 1) * SPEC_INNER_W / SPEC_CHAN_SPAN;
  if (cx < 0)
    cx = 0;
  if (cx > SPEC_INNER_W)
    cx = SPEC_INNER_W;
  return cx;
}

static int crowded_row(void) {
  int best = 0;
  for (int i = 1; i < s_row_count; i++) {
    if (s_row_ap_count[i] > s_row_ap_count[best])
      best = i;
  }
  return best;
}

static int recommended_channel(void) {
  int best_ch = REC_CANDIDATES[0];
  int best_load = -1;
  for (size_t c = 0; c < REC_CANDIDATE_COUNT; c++) {
    int cand = REC_CANDIDATES[c];
    int load = 0;
    for (int i = 0; i < s_row_count; i++) {
      int d = (int)s_row_channel[i] - cand;
      if (d < 0)
        d = -d;
      if (d <= REC_CANDIDATE_SPAN)
        load += s_row_ap_count[i];
    }
    if (best_load < 0 || load < best_load) {
      best_load = load;
      best_ch = cand;
    }
  }
  return best_ch;
}

static void build_hump(lv_obj_t *panel, int row) {
  int cx = channel_center_x(s_row_channel[row]);
  int hw = SPEC_INNER_W * SPEC_HUMP_HALF_NUM / SPEC_HUMP_HALF_DEN;
  int level = s_row_ap_count[row];
  if (level > OCCUPANCY_FULL_APS)
    level = OCCUPANCY_FULL_APS;
  int h = SPEC_HUMP_MAX_H * level / OCCUPANCY_FULL_APS;

  for (int k = 0; k < SPEC_HUMP_PTS; k++) {
    int t = k * 1000 / (SPEC_HUMP_PTS - 1);
    int px = cx - hw + (2 * hw * t) / 1000;
    int u = 2 * t - 1000;
    int f = 1000 - (u * u) / 1000;
    if (f < 0)
      f = 0;
    if (px < 0)
      px = 0;
    if (px > SPEC_INNER_W)
      px = SPEC_INNER_W;
    s_hump_pts[row][k].x = px;
    s_hump_pts[row][k].y = SPEC_BASELINE_Y - h * f / 1000;
  }

  lv_obj_t *line = lv_line_create(panel);
  lv_obj_set_size(line, SPEC_INNER_W, SPEC_INNER_H);
  lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(line, SPEC_LINE_W, 0);
  lv_obj_set_style_line_color(line, lv_color_hex(s_row_color[row]), 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  lv_line_set_points(line, s_hump_pts[row], SPEC_HUMP_PTS);

  lv_obj_t *num = lv_label_create(panel);
  lv_label_set_text_fmt(num, "%d", s_row_channel[row]);
  lv_obj_set_style_text_color(num, lv_color_hex(s_row_color[row]), 0);
  lv_obj_set_style_text_font(num, &lv_font_montserrat_12, 0);
  lv_obj_set_width(num, SPEC_LABEL_W);
  lv_obj_set_style_text_align(num, LV_TEXT_ALIGN_CENTER, 0);
  int lx = cx - SPEC_LABEL_W / 2;
  if (lx < 0)
    lx = 0;
  if (lx > SPEC_INNER_W - SPEC_LABEL_W)
    lx = SPEC_INNER_W - SPEC_LABEL_W;
  lv_obj_align(num, LV_ALIGN_TOP_LEFT, lx, SPEC_BASELINE_Y + SPEC_LABEL_Y_OFS);
}

static void build_spectrum(void) {
  ui_chrome_header(s_screen, "Channels", "/assets/icons/graphic_eq.bin");

  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_set_size(panel, SPEC_PANEL_W, SPEC_PANEL_H);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, SPEC_PANEL_TOP);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(panel, SPEC_PANEL_RADIUS, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(SPEC_PANEL_BG_HEX), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(panel, SPEC_PANEL_PAD, 0);
  lv_obj_set_style_shadow_width(panel, SPEC_GLOW_W, 0);
  lv_obj_set_style_shadow_color(panel, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(panel, LV_OPA_30, 0);

  lv_obj_t *base = lv_obj_create(panel);
  lv_obj_remove_flag(base, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(base, SPEC_INNER_W, 2);
  lv_obj_align(base, LV_ALIGN_TOP_LEFT, 0, SPEC_BASELINE_Y);
  lv_obj_set_style_border_width(base, 0, 0);
  lv_obj_set_style_radius(base, 0, 0);
  lv_obj_set_style_bg_color(base, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(base, LV_OPA_40, 0);

  for (int i = 0; i < s_row_count; i++)
    build_hump(panel, i);

  int cr = crowded_row();
  int rec = recommended_channel();
  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text_fmt(
      hint, "Ch %d crowded  %d AP  pick Ch %d", s_row_channel[cr], s_row_ap_count[cr], rec);
  lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_DIM_HEX), 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, SPEC_HINT_Y);

  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back   " LV_SYMBOL_OK "  Rescan");
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = (menu_component_t){0};

  if (s_scan_state == SCAN_RUNNING) {
    ui_chrome_header(s_screen, "Channels", "/assets/icons/graphic_eq.bin");
    waves_create(s_screen,
                 LV_ALIGN_CENTER,
                 0,
                 SCAN_WAVES_Y_OFS,
                 LV_SYMBOL_WIFI,
                 "/assets/icons/wifi_find.bin");
    lv_obj_t *caption = lv_label_create(s_screen);
    lv_label_set_text(caption, "Scanning...");
    lv_obj_set_style_text_color(caption, current_theme.text_main, 0);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, SCAN_CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else if (s_scan_state == SCAN_FAIL) {
    s_menu = menu_component_create(s_screen, "Channels", "/assets/icons/graphic_eq.bin");
    menu_component_add_item(&s_menu, "/assets/icons/wifi_find.bin", "Scan failed (C5?)");
  } else if (s_row_count == 0) {
    s_menu = menu_component_create(s_screen, "Channels", "/assets/icons/graphic_eq.bin");
    menu_component_add_item(&s_menu, "/assets/icons/wifi_find.bin", "No networks found");
  } else {
    build_spectrum();
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_CHANNELS)
    return;
  build_screen();
  ESP_LOGI(
      TAG, "channel analysis: state=%d, %d occupied channel(s)", (int)s_scan_state, s_row_count);
}

static void wifi_channel_task(void *arg) {
  (void)arg;
  int rows = 0;

  for (int i = 0; i < SCAN_SIM_STEPS; i++)
    vTaskDelay(pdMS_TO_TICKS(SCAN_SIM_STEP_MS));

  for (size_t i = 0; i < MOCK_CHANNEL_COUNT && rows < MAX_ROWS; i++) {
    s_row_channel[rows] = MOCK_CHANNELS[i].channel;
    s_row_ap_count[rows] = MOCK_CHANNELS[i].ap_count;
    s_row_rssi[rows] = MOCK_CHANNELS[i].best_rssi;
    s_row_color[rows] = color_for_count(MOCK_CHANNELS[i].ap_count);
    rows++;
  }

  s_row_count = rows;
  s_scan_state = SCAN_DONE;
  s_scanning = false;
  lv_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }

  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (down && !s_btn_down_last)
    menu_component_next(&s_menu);
  if (up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if ((back && !s_btn_back_last) || (left && !s_btn_left_last))
    ui_switch_screen(SCREEN_WIFI_MENU);

  if (((ok && !s_btn_ok_last) || (right && !s_btn_right_last)) && !s_scanning) {
    ui_wifi_channel_open();
    return;
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_wifi_channel_open(void) {
  s_scan_state = SCAN_RUNNING;
  s_row_count = 0;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreatePinnedToCore(wifi_channel_task, "wifi_chan", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_UI) !=
        pdPASS) {
      s_scanning = false;
      s_scan_state = SCAN_FAIL;
      build_screen();
    }
  }

  ESP_LOGI(TAG, "Channel analysis screen opened (mock scan)");
}
