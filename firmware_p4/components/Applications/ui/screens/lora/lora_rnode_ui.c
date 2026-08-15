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

#include "lora_rnode_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_random.h"

#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "LORA_RNODE";

#define DATA_TICK_MS 700
#define ENTRY_MS     220

#define SIG_GREEN 0x00E676
#define COL_DIM   0x8A8594

#define CARD_W      216
#define CFG_CARD_H  110
#define CNT_CARD_H  82
#define CARD_RADIUS 13
#define CARD_PAD    12
#define CARD_GAP    12
#define BODY_TOP_Y  50

#define PANEL_SHADOW_W      14
#define PANEL_SHADOW_SPREAD (-3)

#define DOT_SIZE 9
#define DOT_MS   680

#define ICON_ANTENNA "/assets/icons/settings_input_antenna.bin"

#define RX_STEP_MAX 3
#define TX_STEP_MAX 2
#define TX_CHANCE   3
#define LAST_EVERY  4

#define RSSI_BASE (-92)
#define RSSI_SPAN 9
#define SNR_BASE  85
#define SNR_SPAN  21
#define SNR_BIAS  10

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_counter_lbl = NULL;
static lv_obj_t *s_last_lbl = NULL;
static lv_timer_t *s_data_timer = NULL;

static unsigned long s_rx = 0;
static unsigned long s_tx = 0;
static int s_tick = 0;
static int s_last_rssi = RSSI_BASE;
static int s_last_snr = SNR_BASE;

static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h, lv_color_t accent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, accent, 0);
  lv_obj_set_style_shadow_color(p, accent, 0);
  lv_obj_set_style_shadow_width(p, PANEL_SHADOW_W, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, PANEL_SHADOW_SPREAD, 0);
  lv_obj_set_style_pad_all(p, CARD_PAD, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(p, 3, 0);
  return p;
}

static lv_obj_t *readout_line(lv_obj_t *parent, const char *text, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  return l;
}

static void build_config_card(lv_obj_t *parent, lv_color_t accent) {
  lv_obj_t *card = lit_panel(parent, CARD_W, CFG_CARD_H, accent);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, BODY_TOP_Y);

  lv_obj_t *head = lv_obj_create(card);
  lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(head, 0, 0);
  lv_obj_set_style_pad_all(head, 0, 0);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 7, 0);

  lv_obj_t *dot = lv_obj_create(head);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(SIG_GREEN), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, dot);
  lv_anim_set_exec_cb(&a, opa_cb);
  lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
  lv_anim_set_duration(&a, DOT_MS);
  lv_anim_set_playback_duration(&a, DOT_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);

  lv_obj_t *title = lv_label_create(head);
  lv_label_set_text(title, "RNode / KISS");
  lv_obj_set_style_text_color(title, accent, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

  readout_line(card, "Freq 915.0 MHz", current_theme.text_main);
  readout_line(card, "SF7   BW 125k   CR 4:5", current_theme.text_main);
  readout_line(card, "TX 17 dBm", current_theme.text_main);
}

static void build_counter_card(lv_obj_t *parent, lv_color_t accent) {
  lv_obj_t *card = lit_panel(parent, CARD_W, CNT_CARD_H, accent);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, BODY_TOP_Y + CFG_CARD_H + CARD_GAP);

  s_counter_lbl = lv_label_create(card);
  lv_label_set_text_fmt(s_counter_lbl, "RX %lu    TX %lu", s_rx, s_tx);
  lv_obj_set_style_text_color(s_counter_lbl, lv_color_hex(SIG_GREEN), 0);
  lv_obj_set_style_text_font(s_counter_lbl, &lv_font_montserrat_14, 0);

  s_last_lbl = lv_label_create(card);
  lv_label_set_text_fmt(
      s_last_lbl, "Last: RSSI %d  SNR %d.%d", s_last_rssi, s_last_snr / 10, s_last_snr % 10);
  lv_obj_set_style_text_color(s_last_lbl, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(s_last_lbl, &lv_font_montserrat_12, 0);
}

static void data_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    if (s_data_timer == t)
      s_data_timer = NULL;
    return;
  }
  if (s_counter_lbl == NULL)
    return;

  s_rx += (esp_random() % (RX_STEP_MAX + 1));
  if ((esp_random() % TX_CHANCE) == 0)
    s_tx += (esp_random() % (TX_STEP_MAX + 1));
  lv_label_set_text_fmt(s_counter_lbl, "RX %lu    TX %lu", s_rx, s_tx);

  s_tick++;
  if (s_tick % LAST_EVERY == 0 && s_last_lbl != NULL) {
    s_last_rssi = RSSI_BASE + (int)(esp_random() % RSSI_SPAN) - (RSSI_SPAN / 2);
    s_last_snr = SNR_BASE + (int)(esp_random() % SNR_SPAN) - SNR_BIAS;
    lv_label_set_text_fmt(
        s_last_lbl, "Last: RSSI %d  SNR %d.%d", s_last_rssi, s_last_snr / 10, s_last_snr % 10);
  }
}

static void build_screen(void) {
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "RNODE", ICON_ANTENNA);

  lv_color_t accent = ui_theme_get_accent();
  build_config_card(s_screen, accent);
  build_counter_card(s_screen, accent);

  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT " BACK to LoRa");

  lv_obj_fade_in(s_screen, ENTRY_MS, 0);
}

static void lora_rnode_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_LORA_CHAT);
      break;
    default:
      break;
  }
}

void ui_lora_rnode_open(void) {
  ui_theme_set_protocol(PROTOCOL_LORA);

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_counter_lbl = NULL;
  s_last_lbl = NULL;
  s_rx = 0;
  s_tx = 0;
  s_tick = 0;
  s_last_rssi = RSSI_BASE;
  s_last_snr = SNR_BASE;

  build_screen();

  ui_input_set_screen_handler(lora_rnode_input, NULL);
  if (s_data_timer == NULL)
    s_data_timer = lv_timer_create(data_tick_cb, DATA_TICK_MS, NULL);

  ui_screen_load(s_screen);

  ESP_LOGI(TAG, "LoRa RNode (mock) opened");
}
