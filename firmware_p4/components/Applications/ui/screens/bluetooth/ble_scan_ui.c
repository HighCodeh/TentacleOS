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

#include "ble_scan_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"

#include "assets_manager.h"
#include "ble_scanner.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "oui_lookup.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "BLE_SCAN_UI";

#define STATUS_TICK_MS        50
#define DOT_CYCLE_MS          350
#define SCAN_RESULT_COLOR_HEX 0x00E676
#define BLE_MAX_DEVS          12
#define SCAN_POLL_MS          150
#define SCAN_TIMEOUT_MS       15000
#define BLE_DEV_ICON          "/assets/icons/bluetooth.bin"
#define BLE_SEARCH_ICON       "/assets/icons/bluetooth_searching.bin"
#define BLE_SCAN_TASK_STACK   8192
#define BLE_SCAN_TASK_PRIO    SYS_PRIO_SERVICE_LO
#define HERO_CARD_W           200
#define HERO_CARD_H           108

#define RADAR_BOX    LV_MIN(190, ui_screen_h() - RADAR_TOP_Y - (CHIP_H + 10) - UI_CHROME_FOOTER_H)
#define RADAR_TOP_Y  (UI_CHROME_HEADER_H + 4)
#define RADAR_RING_1 28
#define RADAR_RING_2 55
#define RADAR_RING_3 82
#define RADAR_BLIP_RMIN 14
#define RADAR_BLIP_RMAX 72
#define RADAR_BLIP_SZ   10
#define RADAR_YOU_SZ    10
#define RADAR_START_DEG 234
#define TRIGO_MAX       32767

#define RSSI_MAP_NEAR (-40)
#define RSSI_MAP_FAR  (-92)
#define RSSI_NEAR_DBM (-55)
#define RSSI_FAR_DBM  (-80)
#define BLIP_NEAR_HEX SCAN_RESULT_COLOR_HEX
#define BLIP_MID_HEX  0xF5B13D
#define BLIP_FAR_HEX  0xFF6B6B

#define CHIP_W 208
#define CHIP_H 46

typedef enum { SCAN_RUNNING, SCAN_DONE, SCAN_FAIL } scan_state_t;

typedef struct {
  char name[24];
  int8_t rssi;
  char mac[18];
  char vendor[24];
} ble_dev_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status = NULL;
static lv_timer_t *s_status_timer = NULL;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;
static bool s_showing_radar = false;
static int s_dev_count = 0;
static ble_dev_t s_devs[BLE_MAX_DEVS];
static uint32_t s_scan_start = 0;

static int s_sel = 0;
static lv_obj_t *s_blip_dot[BLE_MAX_DEVS];
static lv_obj_t *s_chip = NULL;
static lv_obj_t *s_chip_name = NULL;
static lv_obj_t *s_chip_meta = NULL;
static lv_obj_t *s_chip_rssi = NULL;

static void scan_status_tick_cb(lv_timer_t *t);
static void ble_scan_input(const input_event_t *ev, void *ctx);

static uint32_t blip_hex(int rssi) {
  if (rssi >= RSSI_NEAR_DBM)
    return BLIP_NEAR_HEX;
  if (rssi >= RSSI_FAR_DBM)
    return BLIP_MID_HEX;
  return BLIP_FAR_HEX;
}

static int rssi_to_radius(int rssi) {
  int span = RSSI_MAP_FAR - RSSI_MAP_NEAR;
  int off = rssi - RSSI_MAP_NEAR;
  int r = RADAR_BLIP_RMIN + off * (RADAR_BLIP_RMAX - RADAR_BLIP_RMIN) / span;
  if (r < RADAR_BLIP_RMIN)
    r = RADAR_BLIP_RMIN;
  if (r > RADAR_BLIP_RMAX)
    r = RADAR_BLIP_RMAX;
  return r;
}

static lv_obj_t *radar_ring(lv_obj_t *parent, int r, lv_opa_t opa) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, r * 2, r * 2);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(o, opa, 0);
  lv_obj_align(o, LV_ALIGN_CENTER, 0, 0);
  return o;
}

static lv_obj_t *radar_dot(lv_obj_t *parent, int sz, lv_color_t c) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, sz, sz);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

static void radar_axis(lv_obj_t *parent, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_bg_color(o, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_20, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_align(o, LV_ALIGN_CENTER, 0, 0);
}

static void build_scan_hero(void) {
  s_showing_radar = false;
  ui_chrome_header(s_screen, "DETECT", BLE_SEARCH_ICON);

  waves_create(s_screen, LV_ALIGN_CENTER, 0, -24, LV_SYMBOL_BLUETOOTH, NULL);

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, "Searching");
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 70);

  ui_chrome_footer(s_screen, "BACK  Cancel");

  s_scan_start = lv_tick_get();
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, 13, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(p, 16, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -4, 0);
  return p;
}

static void build_empty_hero(const char *title, const char *sub) {
  s_showing_radar = false;

  ui_chrome_header(s_screen, "BLE Devices", BLE_SEARCH_ICON);

  lv_obj_t *card = lit_panel(s_screen, HERO_CARD_W, HERO_CARD_H);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_image_dsc_t *dsc = assets_get(BLE_SEARCH_ICON);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  }

  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *s = lv_label_create(card);
  lv_label_set_text(s, sub);
  lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s, current_theme.text_secondary, 0);
  lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);

  ui_chrome_footer(s_screen, "RIGHT Rescan   BACK Back");

  lv_obj_fade_in(card, 240, 0);
}

static void update_selection(void) {
  for (int i = 0; i < s_dev_count; i++) {
    if (s_blip_dot[i] == NULL)
      continue;
    bool on = (i == s_sel);
    lv_obj_set_style_outline_width(s_blip_dot[i], on ? 3 : 0, 0);
    lv_obj_set_style_outline_color(s_blip_dot[i], current_theme.border_accent, 0);
    lv_obj_set_style_outline_opa(s_blip_dot[i], on ? LV_OPA_50 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_pad(s_blip_dot[i], on ? 2 : 0, 0);
    lv_obj_set_style_shadow_width(s_blip_dot[i], on ? 12 : 0, 0);
    lv_obj_set_style_shadow_color(s_blip_dot[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_opa(s_blip_dot[i], on ? LV_OPA_60 : LV_OPA_TRANSP, 0);
  }

  if (s_sel < 0 || s_sel >= s_dev_count)
    return;
  if (s_chip_name != NULL)
    lv_label_set_text(s_chip_name, s_devs[s_sel].name);
  if (s_chip_meta != NULL)
    lv_label_set_text_fmt(s_chip_meta, "%s  %.8s", s_devs[s_sel].vendor, s_devs[s_sel].mac);
  if (s_chip_rssi != NULL) {
    lv_label_set_text_fmt(s_chip_rssi, "%d", s_devs[s_sel].rssi);
    lv_obj_set_style_text_color(s_chip_rssi, lv_color_hex(blip_hex(s_devs[s_sel].rssi)), 0);
  }
}

static void build_radar(void) {
  s_showing_radar = true;
  s_sel = 0;

  ui_chrome_header(s_screen, "BLE Devices", BLE_SEARCH_ICON);

  lv_obj_t *cont = lv_obj_create(s_screen);
  lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(cont, RADAR_BOX, RADAR_BOX);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, RADAR_TOP_Y);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);

  radar_axis(cont, RADAR_RING_3 * 2, 1);
  radar_axis(cont, 1, RADAR_RING_3 * 2);

  radar_ring(cont, RADAR_RING_3, LV_OPA_30);
  radar_ring(cont, RADAR_RING_2, LV_OPA_50);
  radar_ring(cont, RADAR_RING_1, LV_OPA_70);

  lv_obj_t *you = radar_dot(cont, RADAR_YOU_SZ, current_theme.border_accent);
  lv_obj_set_style_shadow_width(you, 14, 0);
  lv_obj_set_style_shadow_color(you, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(you, LV_OPA_70, 0);
  lv_obj_align(you, LV_ALIGN_CENTER, 0, 0);

  int step = 360 / s_dev_count;
  for (int i = 0; i < s_dev_count; i++) {
    int deg = RADAR_START_DEG + i * step;
    int r = rssi_to_radius(s_devs[i].rssi);
    int trig_x = lv_trigo_sin((int16_t)(deg + 90));
    int trig_y = lv_trigo_sin((int16_t)deg);
    int dx = r * trig_x / TRIGO_MAX;
    int dy = r * trig_y / TRIGO_MAX;

    s_blip_dot[i] = radar_dot(cont, RADAR_BLIP_SZ, lv_color_hex(blip_hex(s_devs[i].rssi)));
    lv_obj_align(s_blip_dot[i], LV_ALIGN_CENTER, dx, dy);
  }

  s_chip = lit_panel(s_screen, CHIP_W, CHIP_H);
  lv_obj_align(s_chip, LV_ALIGN_BOTTOM_MID, 0, -(UI_CHROME_FOOTER_H + 6));
  lv_obj_set_style_pad_all(s_chip, 8, 0);

  s_chip_name = lv_label_create(s_chip);
  lv_obj_set_style_text_font(s_chip_name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_chip_name, current_theme.text_main, 0);
  lv_obj_align(s_chip_name, LV_ALIGN_TOP_LEFT, 0, 0);

  s_chip_meta = lv_label_create(s_chip);
  lv_obj_set_style_text_font(s_chip_meta, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_chip_meta, current_theme.text_secondary, 0);
  lv_obj_align(s_chip_meta, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  s_chip_rssi = lv_label_create(s_chip);
  lv_obj_set_style_text_font(s_chip_rssi, &lv_font_montserrat_16, 0);
  lv_obj_align(s_chip_rssi, LV_ALIGN_RIGHT_MID, 0, 0);

  ui_chrome_footer(s_screen, "L/R Select   OK Info   BACK");

  update_selection();
  lv_obj_fade_in(cont, 240, 0);
  lv_obj_fade_in(s_chip, 280, 0);
}

static void build_results(void) {
  if (s_scan_state == SCAN_FAIL) {
    build_empty_hero("Scan failed", "Radio error (C5?)");
    return;
  }
  if (s_dev_count == 0) {
    build_empty_hero("No devices found", "Try rescanning");
    return;
  }
  build_radar();
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status = NULL;
  s_chip = NULL;
  s_chip_name = NULL;
  s_chip_meta = NULL;
  s_chip_rssi = NULL;
  for (int i = 0; i < BLE_MAX_DEVS; i++)
    s_blip_dot[i] = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  if (s_scan_state == SCAN_RUNNING)
    build_scan_hero();
  else
    build_results();

  if (s_status_timer == NULL)
    s_status_timer = lv_timer_create(scan_status_tick_cb, STATUS_TICK_MS, NULL);

  ui_input_set_screen_handler(ble_scan_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void open_detail(int idx) {
  if (idx < 0 || idx >= s_dev_count)
    return;
  char body[96];
  snprintf(body,
           sizeof(body),
           "Vendor %s\nRSSI %d dBm\nMAC %s",
           s_devs[idx].vendor,
           s_devs[idx].rssi,
           s_devs[idx].mac);
  ui_feedback(UI_FB_SELECT);
  msgbox_open_info(BLE_DEV_ICON, s_devs[idx].name, body, current_theme.border_accent);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_BLE_SCAN)
    return;
  build_screen();
  if (s_scan_state == SCAN_DONE && s_dev_count > 0)
    ui_feedback(UI_FB_READ);
  ESP_LOGI(TAG, "scan finished: state=%d, %d device(s)", (int)s_scan_state, s_dev_count);
}

static void collect_results(void) {
  uint16_t n = 0;
  bluetooth_service_scan_result_t *res = ble_scanner_get_results(&n);
  int count = 0;
  if (res != NULL) {
    for (uint16_t i = 0; i < n && count < BLE_MAX_DEVS; i++) {
      const bluetooth_service_scan_result_t *d = &res[i];
      snprintf(s_devs[count].name,
               sizeof(s_devs[count].name),
               "%.23s",
               (d->name[0] != '\0') ? d->name : "(unknown)");
      s_devs[count].rssi = (int8_t)d->rssi;
      snprintf(s_devs[count].mac,
               sizeof(s_devs[count].mac),
               "%02X:%02X:%02X:%02X:%02X:%02X",
               d->addr[5],
               d->addr[4],
               d->addr[3],
               d->addr[2],
               d->addr[1],
               d->addr[0]);
      snprintf(
          s_devs[count].vendor, sizeof(s_devs[count].vendor), "%.23s", oui_get_vendor(d->addr));
      count++;
    }
  }
  s_dev_count = count;
  ble_scanner_free_results();
}

static void ble_scan_task(void *arg) {
  (void)arg;

  if (!ble_scanner_start()) {
    s_dev_count = 0;
    s_scan_state = SCAN_FAIL;
    s_scanning = false;
    ui_async_call(scan_done_cb, NULL);
    vTaskDelete(NULL);
    return;
  }

  uint16_t dummy = 0;
  uint32_t waited = 0;
  while (ble_scanner_get_results(&dummy) == NULL && waited < SCAN_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(SCAN_POLL_MS));
    waited += SCAN_POLL_MS;
  }

  if (waited >= SCAN_TIMEOUT_MS) {
    ble_scanner_free_results();
    s_dev_count = 0;
    s_scan_state = SCAN_FAIL;
  } else {
    collect_results();
    s_scan_state = SCAN_DONE;
  }

  s_scanning = false;
  ui_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void tick_scan_status(void) {
  if (!s_status)
    return;
  uint32_t el = lv_tick_get() - s_scan_start;
  int dots = (el / DOT_CYCLE_MS) % 4;
  char buf[20];
  snprintf(buf,
           sizeof(buf),
           "Searching%s",
           dots == 1   ? "."
           : dots == 2 ? ".."
           : dots == 3 ? "..."
                       : "");
  lv_label_set_text(s_status, buf);
}

static void radar_step(int dir) {
  if (s_dev_count <= 0)
    return;
  s_sel = (s_sel + dir + s_dev_count) % s_dev_count;
  ui_feedback(UI_FB_NAV);
  update_selection();
}

static void scan_status_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_status_timer = NULL;
    return;
  }
  if (s_scan_state == SCAN_RUNNING)
    tick_scan_status();
}

static void ble_scan_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (ev->button == INPUT_BTN_BACK) {
    if (press)
      ui_switch_screen(SCREEN_BLE_MENU);
    return;
  }

  if (s_scan_state == SCAN_RUNNING) {
    return;
  }

  if (s_showing_radar) {
    switch (ev->button) {
      case INPUT_BTN_UP:
      case INPUT_BTN_LEFT:
        if (nav)
          radar_step(-1);
        break;
      case INPUT_BTN_DOWN:
      case INPUT_BTN_RIGHT:
        if (nav)
          radar_step(1);
        break;
      case INPUT_BTN_OK:
        if (press)
          open_detail(s_sel);
        break;
      default:
        break;
    }
    return;
  }

  switch (ev->button) {
    case INPUT_BTN_RIGHT:
      if (press && !s_scanning)
        ui_ble_scan_open();
      break;
    default:
      break;
  }
}

void ui_ble_scan_open(void) {
  s_scan_state = SCAN_RUNNING;
  s_dev_count = 0;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreatePinnedToCore(ble_scan_task,
                                "ble_scan",
                                BLE_SCAN_TASK_STACK,
                                NULL,
                                BLE_SCAN_TASK_PRIO,
                                NULL,
                                SYS_CORE_RADIO) != pdPASS) {
      s_scanning = false;
      s_scan_state = SCAN_FAIL;
      build_screen();
      notify(NOTIFY_WARNING, "Scan failed");
    }
  }

  ESP_LOGI(TAG, "BLE scan screen opened");
}
