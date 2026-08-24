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

#include "wifi_signal_locator_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"

#include "menu_component_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "waves_ui.h"

#include "ap_scanner.h"
#include "signal_monitor.h"
#include "wifi_service.h"

static const char *TAG = "WIFI_SIG_LOC_UI";

#define ARC_ANIM_MS    260
#define ENTRY_FADE_MS  240
#define SIGNAL_POLL_MS 300

#define HEADER_ICON "/assets/icons/wifi_find.bin"
#define DBM_FONT    "A:assets/fonts/Inter.bin"

#define AP_PICK_MAX     MENU_COMP_MAX_ITEMS
#define SSID_BUF_LEN    33
#define TASK_STACK_SIZE 8192
#define TASK_PRIORITY   SYS_PRIO_SERVICE_LO
#define WAVES_Y_OFS     -6
#define CAPTION_Y_OFS   78

#define ARC_SIZE     118
#define ARC_WIDTH    13
#define ARC_ROTATION 270
#define RANGE_MIN    0
#define RANGE_MAX    100

#define TARGET_Y  (UI_CHROME_HEADER_H + 6)
#define ARC_TOP_Y (UI_CHROME_HEADER_H + 30)
#define PILL_Y    (ARC_TOP_Y + ARC_SIZE + 10)
#define PROX_Y    (PILL_Y + 32)

#define RSSI_MIN   (-92)
#define RSSI_MAX   (-28)
#define RSSI_START (-70)

#define PCT_FAR 34
#define PCT_MID 67
#define PCT_HOT 90

#define FAR_COLOR  0xE53935
#define MID_COLOR  0xFFB300
#define NEAR_COLOR 0x00E676
#define WARM_COLOR 0x00E676
#define COLD_COLOR 0x29B6F6

typedef enum { SL_PICK_SCANNING, SL_PICK_LIST, SL_LOCATING } sl_state_t;

typedef struct {
  char ssid[SSID_BUF_LEN];
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
} pick_ap_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_obj_t *s_arc = NULL;
static lv_obj_t *s_dbm_label = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_prox_label = NULL;
static lv_font_t *s_big_font = NULL;

static sl_state_t s_state = SL_PICK_SCANNING;

static int s_ap_count = 0;
static pick_ap_t s_aps[AP_PICK_MAX];
static bool s_pick_scanning = false;

static uint8_t s_sel_bssid[6];
static uint8_t s_sel_channel = 0;
static char s_sel_ssid[SSID_BUF_LEN] = {0};

static int s_rssi = RSSI_START;
static volatile int8_t s_shared_rssi = RSSI_START;

static volatile bool s_poll_run = false;
static volatile bool s_worker_busy = false;
static volatile bool s_restart_pending = false;

static void wifi_signal_locator_input(const input_event_t *ev, void *ctx);

static void ap_row_text(const pick_ap_t *ap, char *dst, size_t n) {
  if (ap->ssid[0] != '\0')
    snprintf(dst, n, "%s", ap->ssid);
  else
    snprintf(dst, n, "%02X:%02X:%02X CH%d", ap->bssid[3], ap->bssid[4], ap->bssid[5], ap->channel);
}

static int rssi_to_pct(int rssi) {
  int pct = (rssi - RSSI_MIN) * 100 / (RSSI_MAX - RSSI_MIN);
  if (pct < RANGE_MIN)
    pct = RANGE_MIN;
  if (pct > RANGE_MAX)
    pct = RANGE_MAX;
  return pct;
}

static lv_color_t heat_color(int pct) {
  if (pct < PCT_FAR)
    return lv_color_hex(FAR_COLOR);
  if (pct < PCT_MID)
    return lv_color_hex(MID_COLOR);
  return lv_color_hex(NEAR_COLOR);
}

static const char *proximity_text(int pct) {
  if (pct < PCT_FAR)
    return "FAR";
  if (pct < PCT_MID)
    return "GETTING CLOSE";
  if (pct < PCT_HOT)
    return "NEAR";
  return "VERY CLOSE";
}

static void arc_anim_exec_cb(void *obj, int32_t v) {
  lv_arc_set_value((lv_obj_t *)obj, v);
}

static void apply_signal(int prev_rssi) {
  int pct = rssi_to_pct(s_rssi);
  lv_color_t c = heat_color(pct);

  if (s_arc != NULL) {
    lv_obj_set_style_arc_color(s_arc, c, LV_PART_INDICATOR);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_arc);
    lv_anim_set_exec_cb(&a, arc_anim_exec_cb);
    lv_anim_set_values(&a, lv_arc_get_value(s_arc), pct);
    lv_anim_set_duration(&a, ARC_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
  }

  if (s_dbm_label != NULL) {
    lv_label_set_text_fmt(s_dbm_label, "%d", s_rssi);
    lv_obj_set_style_text_color(s_dbm_label, c, 0);
  }

  if (s_hint != NULL) {
    lv_color_t hc;
    const char *ht;
    if (s_rssi > prev_rssi) {
      hc = lv_color_hex(WARM_COLOR);
      ht = LV_SYMBOL_UP " WARMER";
    } else if (s_rssi < prev_rssi) {
      hc = lv_color_hex(COLD_COLOR);
      ht = LV_SYMBOL_DOWN " COLDER";
    } else {
      hc = current_theme.text_secondary;
      ht = LV_SYMBOL_MINUS " HOLD";
    }
    lv_label_set_text(s_hint, ht);
    lv_obj_set_style_text_color(s_hint, hc, 0);
    lv_obj_set_style_bg_color(s_hint, hc, 0);
    lv_obj_set_style_border_color(s_hint, hc, 0);
  }

  if (s_prox_label != NULL)
    lv_label_set_text(s_prox_label, proximity_text(pct));
}

static void reset_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_arc = NULL;
  s_dbm_label = NULL;
  s_hint = NULL;
  s_prox_label = NULL;
  s_menu = (menu_component_t){0};
}

static lv_obj_t *new_base_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

static void build_pick_scanning(void) {
  reset_screen();
  s_screen = new_base_screen();

  ui_chrome_header(s_screen, "LOCATOR", HEADER_ICON);
  waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y_OFS, LV_SYMBOL_WIFI, HEADER_ICON);
  lv_obj_t *cap = lv_label_create(s_screen);
  lv_label_set_text(cap, "Scanning...");
  lv_obj_set_style_text_color(cap, current_theme.text_main, 0);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(cap, LV_ALIGN_CENTER, 0, CAPTION_Y_OFS);
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");

  s_state = SL_PICK_SCANNING;
  ui_input_set_screen_handler(wifi_signal_locator_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void build_pick_list(void) {
  reset_screen();
  s_screen = new_base_screen();

  s_menu = menu_component_create(s_screen, "LOCATOR", HEADER_ICON);
  if (s_ap_count == 0) {
    menu_component_add_item(&s_menu, HEADER_ICON, "No networks found");
  } else {
    for (int i = 0; i < s_ap_count; i++) {
      char row[SSID_BUF_LEN + 16];
      ap_row_text(&s_aps[i], row, sizeof(row));
      menu_component_add_item(&s_menu, HEADER_ICON, row);
    }
  }
  menu_component_set_hint(&s_menu, "OK target   BACK exit");

  s_state = SL_PICK_LIST;
  ui_input_set_screen_handler(wifi_signal_locator_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void build_locator(void) {
  reset_screen();

  if (s_big_font == NULL)
    s_big_font = lv_binfont_create(DBM_FONT);

  s_screen = new_base_screen();

  ui_chrome_header(s_screen, "LOCATOR", HEADER_ICON);
  ui_chrome_footer(s_screen, "BACK: Exit");

  lv_obj_t *target = lv_label_create(s_screen);
  lv_label_set_text_fmt(target, LV_SYMBOL_WIFI "  %s", s_sel_ssid);
  lv_obj_set_style_text_font(target, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(target, current_theme.border_accent, 0);
  lv_obj_set_style_bg_color(target, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(target, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(target, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(target, 1, 0);
  lv_obj_set_style_border_color(target, current_theme.border_accent, 0);
  lv_obj_set_style_pad_hor(target, 14, 0);
  lv_obj_set_style_pad_ver(target, 3, 0);
  lv_obj_align(target, LV_ALIGN_TOP_MID, 0, TARGET_Y);

  const int arc_stack_h = ARC_SIZE + 10 + 32 + 18;
  int arc_top_y = LV_MIN(ARC_TOP_Y, ui_screen_h() - UI_CHROME_FOOTER_H - arc_stack_h);
  int pill_y = arc_top_y + ARC_SIZE + 10;
  int prox_y = pill_y + 32;

  s_arc = lv_arc_create(s_screen);
  lv_obj_set_size(s_arc, ARC_SIZE, ARC_SIZE);
  lv_obj_align(s_arc, LV_ALIGN_TOP_MID, 0, arc_top_y);
  lv_arc_set_rotation(s_arc, ARC_ROTATION);
  lv_arc_set_bg_angles(s_arc, 0, 360);
  lv_arc_set_range(s_arc, RANGE_MIN, RANGE_MAX);
  lv_arc_set_value(s_arc, 0);
  lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_arc, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_arc, heat_color(rssi_to_pct(s_rssi)), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

  s_dbm_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_dbm_label, "%d", s_rssi);
  lv_obj_set_style_text_font(s_dbm_label, s_big_font ? s_big_font : &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(s_dbm_label, current_theme.text_main, 0);
  lv_obj_align_to(s_dbm_label, s_arc, LV_ALIGN_CENTER, 0, -8);

  lv_obj_t *unit = lv_label_create(s_screen);
  lv_label_set_text(unit, "dBm");
  lv_obj_set_style_text_font(unit, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(unit, current_theme.text_secondary, 0);
  lv_obj_align_to(unit, s_arc, LV_ALIGN_CENTER, 0, 16);

  s_hint = lv_label_create(s_screen);
  lv_label_set_text(s_hint, LV_SYMBOL_MINUS " HOLD");
  lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_opa(s_hint, LV_OPA_20, 0);
  lv_obj_set_style_radius(s_hint, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_hint, 1, 0);
  lv_obj_set_style_pad_hor(s_hint, 12, 0);
  lv_obj_set_style_pad_ver(s_hint, 3, 0);
  lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, pill_y);

  s_prox_label = lv_label_create(s_screen);
  lv_label_set_text(s_prox_label, proximity_text(rssi_to_pct(s_rssi)));
  lv_obj_set_style_text_font(s_prox_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_prox_label, current_theme.text_secondary, 0);
  lv_obj_set_style_text_align(s_prox_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_prox_label, LV_ALIGN_TOP_MID, 0, prox_y);

  apply_signal(s_rssi);

  lv_obj_fade_in(s_screen, ENTRY_FADE_MS, 0);

  s_state = SL_LOCATING;
  ui_input_set_screen_handler(wifi_signal_locator_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

static void signal_update_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_SIGNAL_LOCATOR || s_state != SL_LOCATING) {
    s_poll_run = false;
    return;
  }
  int prev = s_rssi;
  s_rssi = s_shared_rssi;
  apply_signal(prev);
}

static void signal_worker(void *arg) {
  (void)arg;
  for (;;) {
    s_restart_pending = false;
    uint8_t bssid[6];
    uint8_t channel;
    memcpy(bssid, s_sel_bssid, sizeof(bssid));
    channel = s_sel_channel;

    signal_monitor_start(bssid, channel);

    while (s_poll_run && !s_restart_pending) {
      s_shared_rssi = signal_monitor_get_rssi();
      ui_async_call(signal_update_cb, NULL);
      vTaskDelay(pdMS_TO_TICKS(SIGNAL_POLL_MS));
    }

    signal_monitor_stop();
    if (!s_restart_pending)
      break;
  }
  s_worker_busy = false;
  vTaskDelete(NULL);
}

static void start_locating(void) {
  s_rssi = RSSI_START;
  s_shared_rssi = RSSI_START;
  build_locator();

  s_poll_run = true;
  if (!s_worker_busy) {
    s_worker_busy = true;
    s_restart_pending = false;
    if (xTaskCreatePinnedToCore(
            signal_worker, "sig_mon", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
        pdPASS) {
      s_worker_busy = false;
    }
  } else {
    s_restart_pending = true;
  }
  ESP_LOGI(TAG, "signal locator target %s ch %d", s_sel_ssid, s_sel_channel);
}

static void ap_pick_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_SIGNAL_LOCATOR || s_state != SL_PICK_SCANNING)
    return;
  build_pick_list();
  if (s_ap_count > 0)
    ui_feedback(UI_FB_READ);
}

static void ap_pick_task(void *arg) {
  (void)arg;
  int n = 0;

  wifi_service_start();
  if (ap_scanner_start()) {
    uint16_t count = 0;
    wifi_ap_record_t *recs = ap_scanner_get_results(&count);
    if (recs != NULL) {
      for (uint16_t i = 0; i < count && n < AP_PICK_MAX; i++) {
        pick_ap_t *ap = &s_aps[n++];
        strncpy(ap->ssid, (const char *)recs[i].ssid, sizeof(ap->ssid) - 1);
        ap->ssid[sizeof(ap->ssid) - 1] = '\0';
        memcpy(ap->bssid, recs[i].bssid, sizeof(ap->bssid));
        ap->channel = recs[i].primary;
        ap->rssi = recs[i].rssi;
      }
    }
    ap_scanner_free_results();
  }

  s_ap_count = n;
  s_pick_scanning = false;
  ui_async_call(ap_pick_done_cb, NULL);
  vTaskDelete(NULL);
}

static void select_target(int idx) {
  if (idx < 0 || idx >= s_ap_count)
    return;
  const pick_ap_t *ap = &s_aps[idx];
  memcpy(s_sel_bssid, ap->bssid, sizeof(s_sel_bssid));
  s_sel_channel = ap->channel;
  strncpy(s_sel_ssid, ap->ssid, sizeof(s_sel_ssid) - 1);
  s_sel_ssid[sizeof(s_sel_ssid) - 1] = '\0';
  start_locating();
}

static void leave_screen(void) {
  s_poll_run = false;
  ui_switch_screen(SCREEN_WIFI_MENU);
}

static void wifi_signal_locator_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_state) {
    case SL_PICK_SCANNING:
      if ((ev->button == INPUT_BTN_BACK) && press)
        ui_switch_screen(SCREEN_WIFI_MENU);
      break;

    case SL_PICK_LIST:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav)
            menu_component_next(&s_menu);
          break;
        case INPUT_BTN_UP:
          if (nav)
            menu_component_prev(&s_menu);
          break;
        case INPUT_BTN_BACK:
          if (press)
            ui_switch_screen(SCREEN_WIFI_MENU);
          break;
        case INPUT_BTN_OK:
        case INPUT_BTN_RIGHT:
          if (press && s_ap_count > 0)
            select_target(menu_component_get_selected(&s_menu));
          break;
        default:
          break;
      }
      break;

    case SL_LOCATING:
      if ((ev->button == INPUT_BTN_BACK) && press)
        leave_screen();
      break;

    default:
      break;
  }
}

void ui_wifi_signal_locator_open(void) {
  s_poll_run = false;
  s_state = SL_PICK_SCANNING;
  s_ap_count = 0;
  s_rssi = RSSI_START;
  s_shared_rssi = RSSI_START;

  build_pick_scanning();

  if (!s_pick_scanning) {
    s_pick_scanning = true;
    if (xTaskCreatePinnedToCore(
            ap_pick_task, "sig_pick", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
        pdPASS) {
      s_pick_scanning = false;
      s_state = SL_PICK_LIST;
      build_pick_list();
    }
  }
}
