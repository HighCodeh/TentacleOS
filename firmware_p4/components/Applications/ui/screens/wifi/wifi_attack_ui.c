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

#include "wifi_attack_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"

#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

#include "ap_scanner.h"
#include "beacon_spam.h"
#include "wifi_deauther.h"
#include "wifi_flood.h"
#include "wifi_service.h"

static const char *TAG = "WIFI_ATTACK_UI";

#define AP_PICK_MAX       20
#define SSID_BUF_LEN      33
#define AP_ROW_EXTRA      16
#define ELAPSED_TICK_MS   250
#define TASK_STACK_SIZE   8192
#define TASK_PRIORITY     SYS_PRIO_SERVICE_LO
#define DEAUTH_FRAME_TYPE WIFI_DEAUTHER_TYPE_CLASS3
#define DEAUTH_BROADCAST  true

#define BOLT_ICON "/assets/icons/bolt.bin"
#define PICK_ICON "/assets/icons/wifi_find.bin"

typedef enum {
  ATTACK_DEAUTH,
  ATTACK_BEACON_SPAM,
  ATTACK_FLOOD_PROBE,
  ATTACK_FLOOD_AUTH,
  ATTACK_FLOOD_ASSOC,
} attack_kind_t;

typedef struct {
  const char *name;
  attack_kind_t kind;
  bool needs_target;
} attack_def_t;

static const attack_def_t ATTACKS[] = {
    {"Deauth", ATTACK_DEAUTH, true},
    {"Beacon Spam", ATTACK_BEACON_SPAM, false},
    {"Probe Flood", ATTACK_FLOOD_PROBE, true},
    {"Auth Flood", ATTACK_FLOOD_AUTH, true},
    {"Assoc Flood", ATTACK_FLOOD_ASSOC, true},
};
#define ATTACKS_COUNT (sizeof(ATTACKS) / sizeof(ATTACKS[0]))

static const char *const ATTACK_ICONS[] = {
    "/assets/icons/wifi_off.bin",
    "/assets/icons/wifi_tethering.bin",
    "/assets/icons/wifi_find.bin",
    "/assets/icons/bolt.bin",
    "/assets/icons/wifi_tethering.bin",
};

typedef enum {
  VIEW_LIST,
  VIEW_PICK_SCANNING,
  VIEW_PICK_LIST,
  VIEW_RUNNING,
} view_t;

typedef struct {
  char ssid[SSID_BUF_LEN];
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
} pick_ap_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_elapsed_timer = NULL;
static lv_obj_t *s_elapsed_label = NULL;
static uint32_t s_start_tick = 0;

static view_t s_view = VIEW_LIST;
static int s_attack_idx = 0;

static int s_ap_count = 0;
static pick_ap_t s_aps[AP_PICK_MAX];
static bool s_is_pick_scanning = false;

static attack_kind_t s_pending_kind = ATTACK_DEAUTH;
static attack_kind_t s_running_kind = ATTACK_DEAUTH;
static bool s_is_target_required = false;
static uint8_t s_sel_bssid[6];
static uint8_t s_sel_channel = 0;
static char s_sel_ssid[SSID_BUF_LEN] = {0};

static bool s_is_attack_starting = false;
static bool s_is_attack_started = false;
static bool s_is_attack_stopping = false;
static volatile bool s_is_stop_requested = false;

static void stop_attack_backend(attack_kind_t kind) {
  switch (kind) {
    case ATTACK_DEAUTH:
      wifi_deauther_stop();
      break;
    case ATTACK_BEACON_SPAM:
      beacon_spam_stop();
      break;
    default:
      wifi_flood_stop();
      break;
  }
}

static void wifi_attack_input(const input_event_t *ev, void *ctx);
static void build_list_view(void);
static void build_pick_scanning(void);
static void build_pick_list(void);
static void build_running_view(void);
static void stop_elapsed_timer(void);
static void stop_running_attack(void);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void ap_row_text(const pick_ap_t *ap, char *dst, size_t n) {
  if (ap->ssid[0] != '\0')
    snprintf(dst, n, "%s", ap->ssid);
  else
    snprintf(dst, n, "%02X:%02X:%02X CH%d", ap->bssid[3], ap->bssid[4], ap->bssid[5], ap->channel);
}

static void clear_screen_children(void) {
  stop_elapsed_timer();
  lv_obj_clean(s_screen);
  s_menu = (menu_component_t){0};
  s_elapsed_label = NULL;
}

static void build_list_view(void) {
  clear_screen_children();
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  s_menu = menu_component_create(s_screen, "ATTACKS", BOLT_ICON);
  for (size_t i = 0; i < ATTACKS_COUNT; i++)
    menu_component_add_item(&s_menu, ATTACK_ICONS[i], ATTACKS[i].name);

  fade_in(s_menu.title_bar, 200);
  fade_in(s_menu.items_cont, 200);

  s_view = VIEW_LIST;
}

static void build_pick_scanning(void) {
  clear_screen_children();
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "Target", PICK_ICON);
  waves_create(s_screen, LV_ALIGN_CENTER, 0, -6, LV_SYMBOL_WIFI, PICK_ICON);

  lv_obj_t *caption = lv_label_create(s_screen);
  lv_label_set_text(caption, "Scanning...");
  lv_obj_set_style_text_color(caption, current_theme.text_main, 0);
  lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(caption, LV_ALIGN_CENTER, 0, 78);

  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");

  s_view = VIEW_PICK_SCANNING;
}

static void build_pick_list(void) {
  clear_screen_children();
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  s_menu = menu_component_create(s_screen, "Target", PICK_ICON);
  if (s_ap_count == 0) {
    menu_component_add_item(&s_menu, PICK_ICON, "No networks found");
  } else {
    for (int i = 0; i < s_ap_count; i++) {
      char row[SSID_BUF_LEN + AP_ROW_EXTRA];
      ap_row_text(&s_aps[i], row, sizeof(row));
      menu_component_add_item(&s_menu, PICK_ICON, row);
    }
  }
  menu_component_set_hint(&s_menu, "OK target   BACK exit");

  fade_in(s_menu.title_bar, 200);
  fade_in(s_menu.items_cont, 200);

  s_view = VIEW_PICK_LIST;
}

static void build_running_view(void) {
  clear_screen_children();
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_obj_t *header = ui_chrome_header(s_screen, ATTACKS[s_attack_idx].name, BOLT_ICON);

  lv_obj_t *status_label = lv_label_create(s_screen);
  lv_label_set_text(status_label, "ACTIVE");
  lv_obj_set_style_text_color(status_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 8);

  lv_obj_t *target_label = lv_label_create(s_screen);
  if (s_is_target_required)
    lv_label_set_text_fmt(target_label, "AP: %s  CH %d", s_sel_ssid, s_sel_channel);
  else
    lv_label_set_text(target_label, "Target: broadcast");
  lv_obj_set_style_text_color(target_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(target_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(target_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(target_label, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 32);

  waves_create(s_screen, LV_ALIGN_CENTER, 0, 14, LV_SYMBOL_WIFI, NULL);

  s_elapsed_label = lv_label_create(s_screen);
  lv_label_set_text(s_elapsed_label, "00:00");
  lv_obj_set_style_text_color(s_elapsed_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_elapsed_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(s_elapsed_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_elapsed_label, LV_ALIGN_BOTTOM_MID, 0, -42);

  ui_chrome_footer(s_screen, "BACK to stop");

  fade_in(header, 200);
  fade_in(status_label, 200);
  fade_in(target_label, 200);
  fade_in(s_elapsed_label, 200);

  s_view = VIEW_RUNNING;
  s_start_tick = lv_tick_get();
}

static void stop_elapsed_timer(void) {
  if (s_elapsed_timer != NULL) {
    lv_timer_delete(s_elapsed_timer);
    s_elapsed_timer = NULL;
  }
}

static void elapsed_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    stop_running_attack();
    lv_timer_delete(timer);
    if (s_elapsed_timer == timer)
      s_elapsed_timer = NULL;
    return;
  }
  if (s_view != VIEW_RUNNING || s_elapsed_label == NULL)
    return;
  uint32_t secs = (lv_tick_get() - s_start_tick) / 1000;
  lv_label_set_text_fmt(s_elapsed_label, "%02u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
}

static void attack_started_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_ATTACK_MENU || s_view != VIEW_RUNNING) {
    if (s_is_attack_started)
      stop_running_attack();
    return;
  }
  if (!s_is_attack_started) {
    msgbox_open(BOLT_ICON, "Attack start failed (C5?)", "OK", NULL, NULL);
    build_list_view();
    ui_input_set_screen_handler(wifi_attack_input, NULL);
    return;
  }
  ui_feedback(UI_FB_EMULATE);
  stop_elapsed_timer();
  s_start_tick = lv_tick_get();
  s_elapsed_timer = lv_timer_create(elapsed_tick_cb, ELAPSED_TICK_MS, NULL);
}

static void attack_start_task(void *arg) {
  (void)arg;
  attack_kind_t kind = s_pending_kind;
  bool ok = false;

  switch (kind) {
    case ATTACK_DEAUTH: {
      wifi_ap_record_t rec;
      memset(&rec, 0, sizeof(rec));
      memcpy(rec.bssid, s_sel_bssid, sizeof(rec.bssid));
      rec.primary = s_sel_channel;
      ok = wifi_deauther_start(&rec, DEAUTH_FRAME_TYPE, DEAUTH_BROADCAST);
      break;
    }
    case ATTACK_BEACON_SPAM:
      ok = beacon_spam_start_random();
      break;
    case ATTACK_FLOOD_PROBE:
      ok = wifi_flood_probe_start(s_sel_bssid, s_sel_channel);
      break;
    case ATTACK_FLOOD_AUTH:
      ok = wifi_flood_auth_start(s_sel_bssid, s_sel_channel);
      break;
    case ATTACK_FLOOD_ASSOC:
      ok = wifi_flood_assoc_start(s_sel_bssid, s_sel_channel);
      break;
  }

  s_running_kind = kind;
  if (ok && s_is_stop_requested) {
    stop_attack_backend(kind);
    ok = false;
  }
  s_is_attack_started = ok;
  s_is_attack_starting = false;
  ui_async_call(attack_started_cb, NULL);
  vTaskDelete(NULL);
}

static void attack_stop_task(void *arg) {
  (void)arg;
  stop_attack_backend(s_running_kind);
  s_is_attack_started = false;
  s_is_attack_stopping = false;
  vTaskDelete(NULL);
}

static void begin_attack(attack_kind_t kind) {
  if (s_is_attack_starting)
    return;
  s_pending_kind = kind;
  s_running_kind = kind;
  s_is_stop_requested = false;
  s_is_attack_starting = true;
  s_is_attack_started = false;

  build_running_view();
  ui_input_set_screen_handler(wifi_attack_input, NULL);

  if (xTaskCreatePinnedToCore(attack_start_task,
                              "atk_start",
                              TASK_STACK_SIZE,
                              NULL,
                              TASK_PRIORITY,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    s_is_attack_starting = false;
    s_is_attack_started = false;
    build_list_view();
    ui_input_set_screen_handler(wifi_attack_input, NULL);
  }
}

static void stop_running_attack(void) {
  ESP_LOGI(TAG, "attack stop: %s", ATTACKS[s_attack_idx].name);
  s_is_stop_requested = true;
  if (!s_is_attack_stopping) {
    s_is_attack_stopping = true;
    if (xTaskCreatePinnedToCore(attack_stop_task,
                                "atk_stop",
                                TASK_STACK_SIZE,
                                NULL,
                                TASK_PRIORITY,
                                NULL,
                                SYS_CORE_RADIO) != pdPASS) {
      s_is_attack_stopping = false;
    }
  }
}

static void ap_pick_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_ATTACK_MENU || s_view != VIEW_PICK_SCANNING)
    return;
  build_pick_list();
  ui_input_set_screen_handler(wifi_attack_input, NULL);
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
  s_is_pick_scanning = false;
  ui_async_call(ap_pick_done_cb, NULL);
  vTaskDelete(NULL);
}

static void start_ap_pick(attack_kind_t kind) {
  s_pending_kind = kind;
  s_ap_count = 0;
  build_pick_scanning();
  ui_input_set_screen_handler(wifi_attack_input, NULL);

  if (!s_is_pick_scanning) {
    s_is_pick_scanning = true;
    if (xTaskCreatePinnedToCore(
            ap_pick_task, "atk_pick", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
        pdPASS) {
      s_is_pick_scanning = false;
      build_pick_list();
      ui_input_set_screen_handler(wifi_attack_input, NULL);
    }
  }
}

static void select_target_and_run(int idx) {
  if (idx < 0 || idx >= s_ap_count)
    return;
  const pick_ap_t *ap = &s_aps[idx];
  memcpy(s_sel_bssid, ap->bssid, sizeof(s_sel_bssid));
  s_sel_channel = ap->channel;
  strncpy(s_sel_ssid, ap->ssid, sizeof(s_sel_ssid) - 1);
  s_sel_ssid[sizeof(s_sel_ssid) - 1] = '\0';
  s_is_target_required = true;
  begin_attack(s_pending_kind);
}

static void wifi_attack_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_view) {
    case VIEW_LIST:
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
          if (press) {
            int sel = menu_component_get_selected(&s_menu);
            if (sel >= 0 && sel < (int)ATTACKS_COUNT) {
              s_attack_idx = sel;
              if (ATTACKS[sel].needs_target) {
                start_ap_pick(ATTACKS[sel].kind);
              } else {
                s_is_target_required = false;
                begin_attack(ATTACKS[sel].kind);
              }
            }
          }
          break;
        default:
          break;
      }
      break;

    case VIEW_PICK_SCANNING:
      if ((ev->button == INPUT_BTN_BACK) && press)
        build_list_view();
      break;

    case VIEW_PICK_LIST:
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
            build_list_view();
          break;
        case INPUT_BTN_OK:
        case INPUT_BTN_RIGHT:
          if (press && s_ap_count > 0)
            select_target_and_run(menu_component_get_selected(&s_menu));
          break;
        default:
          break;
      }
      break;

    case VIEW_RUNNING:
      if ((ev->button == INPUT_BTN_BACK) && press) {
        stop_running_attack();
        build_list_view();
      }
      break;

    default:
      break;
  }
}

void ui_wifi_attack_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  stop_elapsed_timer();
  s_view = VIEW_LIST;
  s_is_attack_starting = false;
  s_is_attack_started = false;
  s_is_attack_stopping = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  build_list_view();

  ui_input_set_screen_handler(wifi_attack_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
