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

#include "subghz_send_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "lvgl.h"

#include "assets_manager.h"
#include "led_control.h"
#include "input_manager.h"
#include "notify_ui.h"
#include "octobit_ui.h"
#include "subghz_replay.h"
#include "subghz_receiver.h"
#include "subghz_storage.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

static const char *TAG = "SUBGHZ_SEND_UI";

#define ANTENNA_ICON       "/assets/icons/settings_input_antenna.bin"
#define WAVES_ICON         "/assets/icons/waves.bin"
#define REFRESH_TICK_MS    50
#define SIGNAL_MAX         64
#define LIST_PAD_SIDE      8
#define LIST_PAD_ROW       6
#define LIST_SB_W          4
#define LIST_SB_RADIUS     2
#define ROW_H              44
#define ROW_RADIUS         10
#define ROW_PAD_H          10
#define ROW_COL_GAP        10
#define ROW_GLYPH_SLOT     28
#define ROW_GLOW_W         14
#define ROW_GLOW_SPREAD    -2
#define DETAIL_PAD         10
#define DETAIL_GAP         6
#define DETAIL_KEY_SIZE    112
#define STATUS_PAD         6
#define STATUS_RADIUS      8
#define FREQ_MHZ_SCALE     1000000UL
#define FREQ_DECIMAL_SCALE 10000UL
#define HINT_LIST          "UP/DOWN choose   OK open   BACK exit"

typedef enum { SUBGHZ_SEND_LIST, SUBGHZ_SEND_DETAIL, SUBGHZ_SEND_VIEW_COUNT } subghz_send_view_t;

EXT_RAM_BSS_ATTR static subghz_storage_entry_t s_sigs[SIGNAL_MAX];
EXT_RAM_BSS_ATTR static lv_obj_t *s_rows[SIGNAL_MAX];
EXT_RAM_BSS_ATTR static lv_obj_t *s_row_name[SIGNAL_MAX];
EXT_RAM_BSS_ATTR static lv_obj_t *s_row_val[SIGNAL_MAX];
static lv_obj_t *s_screen;
static lv_obj_t *s_list;
static lv_obj_t *s_status_label;
static lv_obj_t *s_hint_label;
static lv_timer_t *s_refresh_timer;
static subghz_replay_t s_replay;
static subghz_replay_t s_pending_replay;
static subghz_send_view_t s_view;
static int s_sel;
static int s_sig_count;
static bool s_owns_tx;
static bool s_is_pressed;
static bool s_is_repeating;
static bool s_is_active;
static bool s_is_direct;
static uint32_t s_sent_count;
static char s_pending_name[SUBGHZ_STORAGE_NAME_MAX];

static void build_list(void);
static void build_detail(const char *name);

static void fmt_mhz(uint32_t hz, char *out, size_t size) {
  snprintf(out,
           size,
           "%lu.%02lu MHz",
           (unsigned long)(hz / FREQ_MHZ_SCALE),
           (unsigned long)((hz % FREQ_MHZ_SCALE) / FREQ_DECIMAL_SCALE));
}

static void load_signals(void) {
  int count = subghz_storage_list(s_sigs, SIGNAL_MAX);
  s_sig_count = count < 0 ? 0 : count;
}

static void stop_tx(void) {
  s_is_pressed = false;
  s_is_repeating = false;
  s_is_active = false;
  if (s_owns_tx) {
    subghz_tx_stop();
    s_owns_tx = false;
  }
}

static void screen_deleted_cb(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen)
    ui_subghz_send_stop();
}

static void delete_screen(void) {
  lv_obj_t *previous = s_screen;
  s_screen = NULL;
  if (previous != NULL)
    lv_obj_delete(previous);
}

static lv_obj_t *new_screen(void) {
  lv_obj_t *screen = ui_chrome_screen_create();
  lv_obj_add_event_cb(screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);
  return screen;
}

static void style_row(int i, bool sel) {
  lv_obj_t *r = s_rows[i];
  if (r == NULL)
    return;
  if (sel) {
    lv_obj_set_style_bg_color(r, current_theme.bg_secondary, 0);
    lv_obj_set_style_border_color(r, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_color(r, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(r, ROW_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(r, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(r, ROW_GLOW_SPREAD, 0);
    lv_obj_set_style_text_color(s_row_name[i], current_theme.text_main, 0);
    lv_obj_set_style_text_color(s_row_val[i], current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_bg_color(r, current_theme.bg_primary, 0);
    lv_obj_set_style_border_color(r, current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(r, 0, 0);
    lv_obj_set_style_text_color(s_row_name[i], current_theme.text_secondary, 0);
    lv_obj_set_style_text_color(s_row_val[i], current_theme.text_secondary, 0);
  }
}

static void update_selection(void) {
  for (int i = 0; i < s_sig_count; i++)
    style_row(i, i == s_sel);
  if (s_list != NULL && s_rows[s_sel] != NULL) {
    lv_obj_update_layout(s_list);
    lv_obj_scroll_to_view(s_rows[s_sel], LV_ANIM_ON);
  }
}

static void build_list(void) {
  delete_screen();
  s_status_label = NULL;
  s_hint_label = NULL;
  s_list = NULL;

  load_signals();

  s_screen = new_screen();
  ui_chrome_header(s_screen, "SEND", ANTENNA_ICON);

  if (s_sig_count == 0) {
    octobit_create(s_screen, "No saved signals yet");
    s_hint_label = ui_chrome_footer(s_screen, "BACK exit");
    ui_screen_load_owned(&s_screen, s_screen);
    return;
  }

  if (s_sel < 0)
    s_sel = 0;
  if (s_sel >= s_sig_count)
    s_sel = s_sig_count - 1;

  lv_obj_t *cont = lv_obj_create(s_screen);
  s_list = cont;
  lv_obj_set_size(cont, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_left(cont, LIST_PAD_SIDE, 0);
  lv_obj_set_style_pad_right(cont, LIST_PAD_SIDE, 0);
  lv_obj_set_style_pad_row(cont, LIST_PAD_ROW, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_ON);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_bg_color(cont, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(cont, LIST_SB_W, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(cont, LIST_SB_RADIUS, LV_PART_SCROLLBAR);

  for (int i = 0; i < s_sig_count; i++) {
    lv_obj_t *r = lv_obj_create(cont);
    s_rows[i] = r;
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, ROW_H);
    lv_obj_set_style_radius(r, ROW_RADIUS, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(r, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_pad_hor(r, ROW_PAD_H, 0);
    lv_obj_set_style_pad_ver(r, 0, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, ROW_COL_GAP, 0);

    lv_obj_t *slot = lv_obj_create(r);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(slot, ROW_GLYPH_SLOT, ROW_GLYPH_SLOT);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slot, 0, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);

    lv_image_dsc_t *dsc = assets_get(WAVES_ICON);
    if (dsc != NULL) {
      lv_obj_t *img = lv_image_create(slot);
      lv_image_set_src(img, dsc);
      lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
      lv_obj_set_size(img, ROW_GLYPH_SLOT, ROW_GLYPH_SLOT);
      lv_obj_center(img);
      lv_obj_set_style_image_recolor(img, current_theme.border_accent, 0);
      lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    }

    lv_obj_t *name = lv_label_create(r);
    s_row_name[i] = name;
    lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_text(name, s_sigs[i].name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);

    char fbuf[16];
    fmt_mhz(s_sigs[i].frequency, fbuf, sizeof(fbuf));
    lv_obj_t *val = lv_label_create(r);
    s_row_val[i] = val;
    lv_label_set_text(val, fbuf);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  }

  s_hint_label = ui_chrome_footer(s_screen, HINT_LIST);
  update_selection();

  ui_screen_load_owned(&s_screen, s_screen);
}

static lv_obj_t *detail_label(lv_obj_t *parent, const char *text, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, lv_pct(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

static void build_detail(const char *name) {
  stop_tx();
  subghz_replay_free(&s_replay);
  esp_err_t error = ESP_OK;
  if ((s_pending_replay.pulses != NULL || s_pending_replay.raw_file != NULL)) {
    s_replay = s_pending_replay;
    s_pending_replay = (subghz_replay_t){0};
  } else {
    error = subghz_replay_load(name, &s_replay);
  }
  delete_screen();
  s_view = SUBGHZ_SEND_DETAIL;
  s_sent_count = 0;
  s_screen = new_screen();
  ui_chrome_header(s_screen, "TRANSMIT", ANTENNA_ICON);
  lv_obj_t *content = lv_obj_create(s_screen);
  lv_obj_set_size(content, ui_screen_w(), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_set_pos(content, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, DETAIL_PAD, 0);
  lv_obj_set_style_pad_row(content, DETAIL_GAP, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  detail_label(content, name, current_theme.border_accent);
  if (error == ESP_OK) {
    char frequency[SUBGHZ_STORAGE_NAME_MAX];
    fmt_mhz(s_replay.config.frequency_hz, frequency, sizeof(frequency));
    detail_label(content, frequency, current_theme.text_main);
    detail_label(content, s_replay.protocol, current_theme.text_main);
    char key[DETAIL_KEY_SIZE];
    if (s_replay.key[0])
      snprintf(key, sizeof(key), "Key: %s", s_replay.key);
    else if (s_replay.raw_file && !s_replay.count)
      snprintf(key, sizeof(key), "RAW waveform: SD stream");
    else
      snprintf(key, sizeof(key), "RAW waveform: %u pulses", (unsigned)s_replay.count);
    detail_label(content, key, current_theme.text_secondary);
  } else {
    ESP_LOGE(TAG, "Cannot prepare signal: %s", esp_err_to_name(error));
    led_signal_error();
  }
  s_status_label = detail_label(content,
                                error == ESP_OK                  ? "Ready to send"
                                : error == ESP_ERR_NOT_SUPPORTED ? "Protocol cannot be transmitted"
                                                                 : "Cannot load signal",
                                current_theme.border_accent);
  lv_obj_set_style_bg_color(s_status_label, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(s_status_label, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_status_label, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_status_label, 1, 0);
  lv_obj_set_style_radius(s_status_label, STATUS_RADIUS, 0);
  lv_obj_set_style_pad_all(s_status_label, STATUS_PAD, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  s_hint_label = ui_chrome_footer(s_screen, "Hold OK to send   BACK exit");
  ui_screen_load_owned(&s_screen, s_screen);
}

static void start_frame(void) {
  if (s_refresh_timer == NULL || (s_replay.pulses == NULL && s_replay.raw_file == NULL) ||
      s_is_active)
    return;
  s_replay.config.repeat_until_stopped = true;
  esp_err_t error = subghz_replay_send(&s_replay);
  if (error != ESP_OK) {
    // Init may have succeeded before queue submission failed.
    subghz_tx_stop();
    s_owns_tx = false;
    s_is_pressed = false;
    s_is_repeating = false;
    ESP_LOGE(TAG, "Cannot transmit signal: %s", esp_err_to_name(error));
    led_signal_error();
    lv_label_set_text(s_status_label, "Send failed. OK retry");
    return;
  }
  s_owns_tx = true;
  s_is_active = true;
  lv_label_set_text(s_status_label, "Transmitting...");
}

static void refresh_tick_cb(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen) {
    ui_subghz_send_stop();
    return;
  }
  if (s_view != SUBGHZ_SEND_DETAIL)
    return;
  // A modal or a full input queue must not leave a held transmitter running.
  if (s_is_pressed && !input_is_down(INPUT_BTN_OK)) {
    if (s_is_repeating) {
      stop_tx();
      lv_label_set_text(s_status_label, "Released. OK send again");
    }
    s_is_pressed = false;
    s_is_repeating = false;
  }
  if (s_is_active) {
    subghz_tx_status_t status;
    subghz_tx_get_status(&status);
    if (status.is_busy && status.error == ESP_OK)
      return;
    s_is_active = false;
    if (status.error != ESP_OK) {
      stop_tx();
      lv_label_set_text(s_status_label, "Transmission failed. OK retry");
      return;
    }
    ++s_sent_count;
    lv_label_set_text_fmt(s_status_label, "Sent: %lu", (unsigned long)s_sent_count);
  }
  if (s_is_pressed && s_is_repeating)
    start_frame();
}

static void subghz_send_input(const input_event_t *event, void *context) {
  (void)context;
  bool is_press = event->action == INPUT_ACTION_PRESS;
  bool is_nav = is_press || event->action == INPUT_ACTION_REPEAT;
  if (s_view == SUBGHZ_SEND_LIST) {
    if (is_nav && event->button == INPUT_BTN_DOWN && s_sel < s_sig_count - 1) {
      ++s_sel;
      update_selection();
      ui_feedback(UI_FB_NAV);
    } else if (is_nav && event->button == INPUT_BTN_UP && s_sel > 0) {
      --s_sel;
      update_selection();
      ui_feedback(UI_FB_NAV);
    } else if (is_press && (event->button == INPUT_BTN_OK || event->button == INPUT_BTN_RIGHT) &&
               s_sig_count > 0) {
      build_detail(s_sigs[s_sel].name);
    } else if (is_press && event->button == INPUT_BTN_BACK) {
      ui_switch_screen(SCREEN_SUBGHZ_MENU);
    }
    return;
  }
  if (event->button == INPUT_BTN_BACK && is_press) {
    stop_tx();
    subghz_replay_free(&s_replay);
    if (s_is_direct) {
      ui_switch_screen(SCREEN_SUBGHZ_MENU);
    } else {
      s_view = SUBGHZ_SEND_LIST;
      build_list();
    }
  } else if (event->button == INPUT_BTN_OK) {
    if (is_press) {
      s_is_pressed = true;
      s_is_repeating = true;
      start_frame();
    } else if (event->action == INPUT_ACTION_RELEASE) {
      // Every press uses the same PTT lifecycle, including short taps.
      if (s_is_repeating) {
        stop_tx();
        lv_label_set_text(s_status_label, "Released. OK send again");
      }
      s_is_pressed = false;
      s_is_repeating = false;
    }
  }
}

void ui_subghz_send_stop(void) {
  if (s_refresh_timer != NULL) {
    lv_timer_delete(s_refresh_timer);
    s_refresh_timer = NULL;
  }
  stop_tx();
  subghz_replay_free(&s_replay);
}

esp_err_t ui_subghz_send_capture(uint32_t sequence) {
  char *text = NULL;
  subghz_replay_free(&s_pending_replay);
  subghz_raw_file_t *file = NULL;
  if (subghz_receiver_acquire_raw(sequence, &file) == ESP_OK) {
    subghz_replay_from_raw(file, &s_pending_replay);
    subghz_raw_file_release(file);
    snprintf(s_pending_name, sizeof(s_pending_name), "Unsaved signal");
    return ESP_OK;
  }
  esp_err_t error = subghz_receiver_copy_capture(sequence, &text);
  if (error == ESP_OK)
    error = subghz_replay_prepare(text, &s_pending_replay);
  free(text);
  if (error == ESP_OK)
    snprintf(s_pending_name, sizeof(s_pending_name), "Unsaved signal");
  return error;
}

void ui_subghz_send_select(const char *name) {
  subghz_replay_free(&s_pending_replay);
  snprintf(s_pending_name, sizeof(s_pending_name), "%s", name != NULL ? name : "");
}

void ui_subghz_send_open(void) {
  ui_subghz_send_stop();
  s_sel = 0;
  s_view = SUBGHZ_SEND_LIST;
  s_is_direct = s_pending_name[0] != '\0';
  if (s_is_direct) {
    build_detail(s_pending_name);
    s_pending_name[0] = '\0';
  } else {
    build_list();
  }
  s_refresh_timer = lv_timer_create(refresh_tick_cb, REFRESH_TICK_MS, NULL);
  if (s_refresh_timer == NULL) {
    ESP_LOGE(TAG, "Cannot allocate screen refresh timer");
    led_signal_error();
    notify(NOTIFY_WARNING, "Screen unavailable. BACK to exit");
  }
  ui_input_set_screen_handler(subghz_send_input, NULL);
}
