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

#include "nfc_saved_ui.h"

#include <stdio.h>
#include <string.h>

#include "nfc_protocol_service.h"

#include "lvgl.h"
#include "esp_log.h"

#include "st7789.h"

#include "assets_manager.h"
#include "capture_result_ui.h"
#include "msgbox_ui.h"
#include "nfc_sim.h"
#include "nfc_emulate_ui.h"
#include "nfc_ui_common.h"
#include "notify_ui.h"
#include "led_control.h"
#include "page_dots_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

static const char *TAG = "NFC_SAVED_UI";

#define CARD_H            122
#define PEEK              60
#define SAVED_ICON        "/assets/icons/bookmarks.bin"
#define CARD_ICON         "/assets/icons/nfc.bin"
#define UID_VAL_LEN       40
#define DELETE_REFRESH_MS 100

enum { DT_NONE, DT_VIEW };

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_cont = NULL;
static lv_obj_t *s_cards[NFC_SIM_MAX_SAVED];
static page_dots_t s_dots;
static bool s_has_dots = false;
static int s_count = 0;
static int s_sel = 0;
static bool s_empty = false;

static lv_obj_t *s_ov = NULL;
static lv_obj_t *s_ov_footer = NULL;
static capture_result_t s_cr = {0};
static nfc_sim_card_t s_view_card;
static int s_detail = DT_NONE;
static int s_sel_idx = -1;
static lv_timer_t *s_delete_timer;
static bool s_is_deleting;
static bool s_exit_after_delete;

static void present_choices(void);

static void rebuild_async(void *p) {
  (void)p;
  ui_nfc_saved_open();
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

static void build_empty(void) {
  lv_obj_t *card = lit_panel(s_screen, 200, 104);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 6);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_image_dsc_t *dsc = assets_get(SAVED_ICON);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  }
  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, "No saved cards");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);
  lv_obj_t *s = lv_label_create(card);
  lv_label_set_text(s, "Read a tag first");
  lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s, current_theme.text_secondary, 0);
}

static void relayout(void) {
  for (int i = 0; i < s_count; i++) {
    int y = i * PEEK;
    if (i > s_sel)
      y += (CARD_H - PEEK);
    lv_obj_align(s_cards[i], LV_ALIGN_TOP_MID, 0, y);

    bool sel = (i == s_sel);
    if (sel) {
      lv_obj_set_style_border_width(s_cards[i], 3, 0);
      lv_obj_set_style_border_color(s_cards[i], current_theme.border_accent, 0);
      lv_obj_set_style_bg_opa(s_cards[i], LV_OPA_COVER, 0);
      lv_obj_set_style_shadow_color(s_cards[i], current_theme.border_accent, 0);
      lv_obj_set_style_shadow_width(s_cards[i], 14, 0);
      lv_obj_set_style_shadow_opa(s_cards[i], LV_OPA_50, 0);
      lv_obj_set_style_shadow_spread(s_cards[i], -3, 0);
    } else {
      lv_obj_set_style_border_width(s_cards[i], 1, 0);
      lv_obj_set_style_border_color(s_cards[i], nfc_ui_card_color(nfc_sim_saved_get(i)), 0);
      lv_obj_set_style_shadow_width(s_cards[i], 0, 0);
      lv_obj_set_style_shadow_opa(s_cards[i], LV_OPA_TRANSP, 0);
    }
  }
  int sy = s_sel * PEEK - 6;
  if (sy < 0)
    sy = 0;
  lv_obj_scroll_to_y(s_cont, sy, LV_ANIM_OFF);
  if (s_has_dots)
    page_dots_set(&s_dots, s_sel);
}

static void overlay_close(void) {
  if (s_ov) {
    lv_obj_del(s_ov);
    s_ov = NULL;
  }
  s_cr = (capture_result_t){0};
  s_ov_footer = NULL;
  s_detail = DT_NONE;
}

static void overlay_open(const nfc_sim_card_t *c, int idx) {
  s_view_card = *c;

  s_ov = lv_obj_create(s_screen);
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_center(s_ov);
  lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_90, 0);
  lv_obj_set_style_border_width(s_ov, 0, 0);
  lv_obj_set_style_pad_all(s_ov, 0, 0);

  // Transient card-detail overlay: snapshot header (no rebind) so closing it never
  // dangles the live saved-list's dynamic header underneath.
  ui_chrome_header_overlay(s_ov, "CARD", SAVED_ICON);
  s_ov_footer = ui_chrome_footer(s_ov, "OK  Options    BACK  Back");

  s_detail = DT_VIEW;
  s_sel_idx = idx;
  present_choices();
  ui_feedback(UI_FB_SELECT);
}

static void present_choices(void) {
  char uid[UID_VAL_LEN - sizeof("UID ")];
  static char valbuf[UID_VAL_LEN];
  nfc_sim_format_uid(&s_view_card, uid, sizeof(uid));
  snprintf(valbuf, sizeof(valbuf), "UID %s", uid);

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = CARD_ICON,
      .card_title = s_view_card.name[0] ? s_view_card.name : s_view_card.type,
      .card_sub = s_view_card.type,
      .card_value = valbuf,
      .primary_label = "Emulate",
  };
  const capture_result_action_cfg_t actions[] = {
      {.id = CAP_ACT_PRIMARY, .symbol = LV_SYMBOL_UPLOAD, .label = "Emulate"},
      {.id = CAP_ACT_DISCARD, .symbol = LV_SYMBOL_TRASH, .label = "Delete card + data"},
  };
  s_cr = capture_result_create_custom(s_ov, &cfg, actions, sizeof(actions) / sizeof(actions[0]));

  ui_chrome_footer_set_text(s_ov_footer, LV_SYMBOL_UP LV_SYMBOL_DOWN " Choose   OK Do   BACK Back");
}

static void delete_tick(lv_timer_t *timer) {
  nfc_protocol_status_t status;
  nfc_protocol_service_get_status(&status);
  if (status.running)
    return;
  lv_timer_delete(timer);
  s_delete_timer = NULL;
  s_is_deleting = false;
  if (status.state == NFC_PROTOCOL_STATE_DONE) {
    ESP_LOGI(TAG, "Deleted card and linked SD data");
    led_signal_info();
    ui_feedback(UI_FB_WRITE);
    notify(NOTIFY_INFO, "Card and dumps deleted");
    overlay_close();
    if (!s_exit_after_delete)
      ui_async_call(rebuild_async, NULL);
  } else {
    ESP_LOGW(TAG, "Card deletion incomplete: 0x%x", status.error);
    led_signal_warning();
    notify(NOTIFY_WARNING, "Not completed; check SD and retry");
    ui_chrome_footer_set_text(s_ov_footer, "Check SD, then retry delete   BACK");
  }
  if (s_exit_after_delete)
    ui_switch_screen(SCREEN_NFC_MENU);
}

static void on_del_confirm(bool confirm) {
  if (!confirm || s_is_deleting)
    return;
  s_delete_timer = lv_timer_create(delete_tick, DELETE_REFRESH_MS, NULL);
  if (s_delete_timer == NULL) {
    ESP_LOGE(TAG, "Cannot create deletion status timer");
    led_signal_error();
    notify(NOTIFY_WARNING, "Cannot start deletion; try again");
    return;
  }
  esp_err_t error =
      nfc_protocol_service_delete_saved(s_sel_idx, s_view_card.uid, s_view_card.uid_len);
  if (error == ESP_OK)
    error = nfc_protocol_service_start(NFC_PROTOCOL_OP_CARD_DELETE);
  if (error != ESP_OK) {
    lv_timer_delete(s_delete_timer);
    s_delete_timer = NULL;
    ESP_LOGW(TAG, "Cannot start card deletion: 0x%x", error);
    led_signal_warning();
    notify(NOTIFY_WARNING, "Card changed or NFC busy; reopen");
    return;
  }
  s_is_deleting = true;
  s_exit_after_delete = false;
  ui_chrome_footer_set_text(s_ov_footer, "Deleting card and linked data...");
}

static void nfc_saved_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_is_deleting) {
    if (press && ev->button == INPUT_BTN_BACK) {
      s_exit_after_delete = true;
      nfc_protocol_service_cancel();
      ui_chrome_footer_set_text(s_ov_footer, "Finishing storage operation...");
    }
    return;
  }

  if (s_detail == DT_VIEW) {
    if (ev->button == INPUT_BTN_BACK) {
      if (press)
        overlay_close();
      return;
    }
    switch (ev->button) {
      case INPUT_BTN_DOWN:
        if (nav) {
          capture_result_next(&s_cr);
          ui_feedback(UI_FB_NAV);
        }
        break;
      case INPUT_BTN_UP:
        if (nav) {
          capture_result_prev(&s_cr);
          ui_feedback(UI_FB_NAV);
        }
        break;
      case INPUT_BTN_OK:
        if (press) {
          if (capture_result_selected(&s_cr) == CAP_ACT_PRIMARY) {
            ui_feedback(UI_FB_EMULATE);
            if (strcmp(s_view_card.type, "Texkom") == 0) {
              if (nfc_protocol_service_select_texkom(s_sel_idx) == HB_NFC_OK)
                ui_switch_screen(SCREEN_NFC_TEXKOM_EMULATE);
              else
                msgbox_open(LV_SYMBOL_WARNING, "Cannot load Texkom card", "OK", NULL, NULL);
            } else if (strcmp(s_view_card.type, "SLIX / NFC-V") == 0 ||
                       strcmp(s_view_card.type, "ISO15693 / NFC-V") == 0) {
              if (nfc_protocol_service_select_v(s_sel_idx) == HB_NFC_OK)
                ui_switch_screen(SCREEN_NFC_V_EMULATE);
              else
                msgbox_open(LV_SYMBOL_WARNING, "Cannot load NFC-V card", "OK", NULL, NULL);
            } else if (strcmp(s_view_card.type, "ST25TB") == 0) {
              msgbox_open(LV_SYMBOL_WARNING, "Saved dump emulation unavailable", "OK", NULL, NULL);
            } else {
              ui_nfc_emulate_select_card(&s_view_card, SCREEN_NFC_SAVED);
              ui_switch_screen(SCREEN_NFC_EMULATE);
            }
          } else {
            msgbox_open(LV_SYMBOL_TRASH,
                        "Delete card and all its dumps?",
                        "Delete",
                        "Cancel",
                        on_del_confirm);
          }
        }
        break;
      default:
        break;
    }
    return;
  }

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    case INPUT_BTN_DOWN:
      if (nav && !s_empty && s_sel < s_count - 1) {
        s_sel++;
        relayout();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && !s_empty && s_sel > 0) {
        s_sel--;
        relayout();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press && !s_empty) {
        const nfc_sim_card_t *c = nfc_sim_saved_get(s_sel);
        if (c != NULL)
          overlay_open(c, s_sel);
      }
      break;
    default:
      break;
  }
}

static void screen_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen)
    ui_nfc_saved_stop();
}

void ui_nfc_saved_open(void) {
  ui_nfc_saved_stop();
  s_exit_after_delete = false;
  nfc_sim_init();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_ov = NULL;
  s_ov_footer = NULL;
  s_cr = (capture_result_t){0};
  s_has_dots = false;
  s_detail = DT_NONE;
  s_sel_idx = -1;

  s_screen = lv_obj_create(NULL);
  lv_obj_add_event_cb(s_screen, screen_deleted, LV_EVENT_DELETE, NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "SAVED", SAVED_ICON);

  s_count = nfc_sim_saved_count();
  if (s_count > NFC_SIM_MAX_SAVED)
    s_count = NFC_SIM_MAX_SAVED;
  s_empty = (s_count == 0);
  if (s_sel >= s_count)
    s_sel = s_count > 0 ? s_count - 1 : 0;

  if (s_empty) {
    build_empty();
    ui_chrome_footer(s_screen, "BACK  Back");
  } else {
    s_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_cont, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
    lv_obj_align(s_cont, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
    lv_obj_set_style_bg_opa(s_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cont, 0, 0);
    lv_obj_set_style_pad_all(s_cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_cont, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < s_count; i++) {
      s_cards[i] = nfc_ui_card_panel(s_cont, nfc_sim_saved_get(i));
      lv_obj_set_style_bg_grad_dir(s_cards[i], LV_GRAD_DIR_NONE, 0);
    }
    lv_obj_update_layout(s_screen);
    relayout();

    s_dots = page_dots_create(s_screen, s_count, LV_ALIGN_BOTTOM_MID, 0, -28);
    s_has_dots = true;
    page_dots_set(&s_dots, s_sel);

    ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " Browse   OK Open   BACK Exit");
  }

  ui_input_set_screen_handler(nfc_saved_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}

void ui_nfc_saved_stop(void) {
  if (s_delete_timer != NULL) {
    lv_timer_delete(s_delete_timer);
    s_delete_timer = NULL;
  }
  if (s_is_deleting)
    nfc_protocol_service_stop();
  s_is_deleting = false;
}
