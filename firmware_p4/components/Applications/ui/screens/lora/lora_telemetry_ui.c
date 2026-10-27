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

#include "lora_telemetry_ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "st7789.h"

#include "lora_session.h"
#include "sx1262.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define SPARK_MS 700

#define HDR_TITLE   "TELEMETRY"
#define HDR_ICON    NULL
#define FOOTER_HINT "UP/DOWN   OK NODE   BACK"

#define BODY_TOP UI_CHROME_HEADER_H
#define BODY_H   (ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)

#define ROOT_PAD 8
#define ROOT_GAP 7

#define PANEL_RAD   8
#define PANEL_PAD_H 7
#define PANEL_PAD_T 6
#define PANEL_PAD_B 4
#define PANEL_GAP   2

#define SPARK_W   200
#define SPARK_H   28
#define SPARK_PTS 13
#define SPARK_MG  2

#define BATT_H  20
#define BAR_H   8
#define BAR_RAD 4

#define SNR_OK_DB     5.0f
#define SNR_WARN_DB   0.0f
#define RSSI_OK_DBM   (-70)
#define RSSI_WARN_DBM (-90)

#define NB_MAX     4
#define CHIP_H     22
#define CHIP_RAD   10
#define CHIP_PAD_H 8
#define CHIP_GAP   5
#define GLOW_W     10

#define RSSI_AMP_LO -120
#define RSSI_AMP_HI -30
#define RX_FULL     4

#define COL_ACC2  0xB89AFF
#define COL_CYAN  0x37E0A8
#define COL_WARN  0xFFC23D
#define COL_BAD   0xFF5470
#define COL_TRACK 0x241F31

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_ch_line = NULL;
static lv_obj_t *s_air_line = NULL;
static lv_obj_t *s_ch_val = NULL;
static lv_obj_t *s_air_val = NULL;
static lv_obj_t *s_link_bar = NULL;
static lv_obj_t *s_link_pct = NULL;
static lv_obj_t *s_link_sub = NULL;
static lv_obj_t *s_chips[NB_MAX];
static lv_obj_t *s_chip_name[NB_MAX];
static lv_obj_t *s_chip_val[NB_MAX];
static lv_obj_t *s_nb_empty = NULL;
static lv_timer_t *s_spark_timer = NULL;

static uint8_t s_ch_buf[SPARK_PTS];
static uint8_t s_air_buf[SPARK_PTS];
static lv_point_precise_t s_ch_pts[SPARK_PTS];
static lv_point_precise_t s_air_pts[SPARK_PTS];

static int s_nb = 0;
static int s_nb_count = 0;
static uint16_t s_prev_rx = 0;

static int spark_x(int i) {
  return i * SPARK_W / (SPARK_PTS - 1);
}

static int spark_y(uint8_t amp) {
  if (amp > 100)
    amp = 100;
  return SPARK_MG + (100 - amp) * (SPARK_H - 2 * SPARK_MG) / 100;
}

static void rebuild_points(void) {
  for (int i = 0; i < SPARK_PTS; i++) {
    s_ch_pts[i].x = spark_x(i);
    s_ch_pts[i].y = spark_y(s_ch_buf[i]);
    s_air_pts[i].x = spark_x(i);
    s_air_pts[i].y = spark_y(s_air_buf[i]);
  }
  if (s_ch_line)
    lv_line_set_points(s_ch_line, s_ch_pts, SPARK_PTS);
  if (s_air_line)
    lv_line_set_points(s_air_line, s_air_pts, SPARK_PTS);
}

static void push_sample(uint8_t *buf, uint8_t s) {
  for (int i = 0; i < SPARK_PTS - 1; i++)
    buf[i] = buf[i + 1];
  buf[SPARK_PTS - 1] = s;
}

static uint8_t rssi_to_amp(int16_t rssi) {
  int v = rssi;
  if (v < RSSI_AMP_LO)
    v = RSSI_AMP_LO;
  if (v > RSSI_AMP_HI)
    v = RSSI_AMP_HI;
  return (uint8_t)((v - RSSI_AMP_LO) * 100 / (RSSI_AMP_HI - RSSI_AMP_LO));
}

static void sample_once(bool *running, bool *has_rssi, int16_t *rssi, sx1262_stats_t *st) {
  memset(st, 0, sizeof(*st));
  *rssi = 0;
  *has_rssi = false;
  *running = sx1262_is_running();
  if (*running) {
    if (sx1262_get_stats(st) != ESP_OK)
      memset(st, 0, sizeof(*st));
    int16_t r;
    if (sx1262_get_rssi_inst(&r) == ESP_OK) {
      *rssi = r;
      *has_rssi = true;
    }
  }
}

static void paint_stats(bool running, bool has_rssi, int16_t rssi, const sx1262_stats_t *st) {
  if (s_ch_val) {
    if (has_rssi) {
      char b[16];
      snprintf(b, sizeof(b), "%d dBm", rssi);
      lv_label_set_text(s_ch_val, b);
    } else {
      lv_label_set_text(s_ch_val, "--");
    }
  }
  if (s_air_val) {
    if (running) {
      char b[16];
      snprintf(b, sizeof(b), "%u pkts", (unsigned)st->nb_pkt_received);
      lv_label_set_text(s_air_val, b);
    } else {
      lv_label_set_text(s_air_val, "--");
    }
  }

  uint32_t total = st->nb_pkt_received;
  uint32_t errs = (uint32_t)st->nb_crc_error + (uint32_t)st->nb_header_error;
  uint32_t denom = total + errs;
  int ratio = (running && denom > 0) ? (int)(total * 100 / denom) : 0;

  if (s_link_bar)
    lv_bar_set_value(s_link_bar, running ? ratio : 0, LV_ANIM_OFF);
  if (s_link_pct) {
    if (running) {
      char b[12];
      snprintf(b, sizeof(b), "%d%%", ratio);
      lv_label_set_text(s_link_pct, b);
    } else {
      lv_label_set_text(s_link_pct, "--");
    }
  }
  if (s_link_sub) {
    if (running) {
      char b[16];
      snprintf(b, sizeof(b), "CRC %u", (unsigned)st->nb_crc_error);
      lv_label_set_text(s_link_sub, b);
    } else {
      lv_label_set_text(s_link_sub, "--");
    }
  }
}

static uint32_t nb_fill(const lora_node_t *nd, char *buf, size_t n) {
  if (nd->snr != 0.0f) {
    int t = (int)(nd->snr * 10.0f + (nd->snr >= 0.0f ? 0.5f : -0.5f));
    char sign = (t < 0) ? '-' : '+';
    int at = (t < 0) ? -t : t;
    snprintf(buf, n, "%c%d.%d", sign, at / 10, at % 10);
    if (nd->snr >= SNR_OK_DB)
      return UI_COL_SUCCESS;
    if (nd->snr >= SNR_WARN_DB)
      return COL_WARN;
    return COL_BAD;
  }
  if (nd->rssi != 0) {
    snprintf(buf, n, "%d", nd->rssi);
    if (nd->rssi >= RSSI_OK_DBM)
      return UI_COL_SUCCESS;
    if (nd->rssi >= RSSI_WARN_DBM)
      return COL_WARN;
    return COL_BAD;
  }
  snprintf(buf, n, "--");
  return 0x8A8594; // TODO: not themed (raw hex arg)
}

static void refresh_chips(void) {
  for (int i = 0; i < NB_MAX; i++) {
    if (s_chips[i] == NULL)
      continue;
    bool sel = (i == s_nb) && (i < s_nb_count);
    lv_obj_set_style_border_color(
        s_chips[i], sel ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(s_chips[i], sel ? 2 : 1, 0);
    lv_obj_set_style_shadow_color(s_chips[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(s_chips[i], sel ? GLOW_W : 0, 0);
    lv_obj_set_style_shadow_opa(s_chips[i], sel ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_spread(s_chips[i], sel ? -2 : 0, 0);
    lv_obj_set_style_text_color(
        s_chip_name[i], sel ? current_theme.text_main : current_theme.text_secondary, 0);
  }
}

static void refresh_neighbors(void) {
  uint16_t count = lora_session_node_count();
  if (count > NB_MAX)
    count = NB_MAX;

  for (int i = 0; i < NB_MAX; i++) {
    if (s_chips[i] == NULL)
      continue;
    lora_node_t nd;
    if (i < (int)count && lora_session_node_get((uint16_t)i, &nd)) {
      lv_label_set_text(s_chip_name[i], (nd.name[0] != '\0') ? nd.name : "node");
      char v[16];
      uint32_t col = nb_fill(&nd, v, sizeof(v));
      lv_label_set_text(s_chip_val[i], v);
      lv_obj_set_style_text_color(s_chip_val[i], lv_color_hex(col), 0);
      lv_obj_remove_flag(s_chips[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_chips[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  s_nb_count = (int)count;
  if (s_nb >= s_nb_count)
    s_nb = (s_nb_count > 0) ? s_nb_count - 1 : 0;

  if (s_nb_empty) {
    if (s_nb_count == 0)
      lv_obj_remove_flag(s_nb_empty, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_nb_empty, LV_OBJ_FLAG_HIDDEN);
  }

  refresh_chips();
}

static void spark_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_spark_timer = NULL;
    return;
  }

  bool running;
  bool has_rssi;
  int16_t rssi;
  sx1262_stats_t st;
  sample_once(&running, &has_rssi, &rssi, &st);

  uint8_t ch = has_rssi ? rssi_to_amp(rssi) : 0;
  uint16_t delta = 0;
  if (running) {
    delta = (uint16_t)(st.nb_pkt_received - s_prev_rx);
    s_prev_rx = st.nb_pkt_received;
  }
  uint32_t a = (uint32_t)delta * 100 / RX_FULL;
  if (a > 100)
    a = 100;

  push_sample(s_ch_buf, ch);
  push_sample(s_air_buf, (uint8_t)a);
  rebuild_points();
  paint_stats(running, has_rssi, rssi, &st);
  refresh_neighbors();
}

static lv_obj_t *make_panel(lv_obj_t *root,
                            const char *title,
                            lv_color_t line_col,
                            lv_obj_t **out_val,
                            lv_obj_t **out_line,
                            lv_point_precise_t *pts) {
  lv_obj_t *panel = lv_obj_create(root);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_radius(panel, PANEL_RAD, 0);
  lv_obj_set_style_bg_color(panel, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(panel, current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_pad_left(panel, PANEL_PAD_H, 0);
  lv_obj_set_style_pad_right(panel, PANEL_PAD_H, 0);
  lv_obj_set_style_pad_top(panel, PANEL_PAD_T, 0);
  lv_obj_set_style_pad_bottom(panel, PANEL_PAD_B, 0);
  lv_obj_set_style_pad_row(panel, PANEL_GAP, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *head = lv_obj_create(panel);
  lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(head, 0, 0);
  lv_obj_set_style_pad_all(head, 0, 0);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *cap = lv_label_create(head);
  lv_label_set_text(cap, title);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, current_theme.text_secondary, 0);

  lv_obj_t *val = lv_label_create(head);
  lv_label_set_text(val, "--");
  lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(val, current_theme.text_main, 0);
  *out_val = val;

  lv_obj_t *box = lv_obj_create(panel);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(box, SPARK_W, SPARK_H);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);

  lv_obj_t *line = lv_line_create(box);
  lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_line_set_points(line, pts, SPARK_PTS);
  lv_obj_set_style_line_width(line, 2, 0);
  lv_obj_set_style_line_color(line, line_col, 0);
  lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  *out_line = line;

  return panel;
}

static void lora_telemetry_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_LORA_CHAT);
      break;
    case INPUT_BTN_DOWN:
      if (nav && s_nb_count > 0) {
        s_nb = (s_nb + 1) % s_nb_count;
        refresh_chips();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && s_nb_count > 0) {
        s_nb = (s_nb - 1 + s_nb_count) % s_nb_count;
        refresh_chips();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press)
        ui_feedback(UI_FB_SELECT);
      break;
    default:
      break;
  }
}

void ui_lora_telemetry_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  if (s_spark_timer != NULL) {
    lv_timer_delete(s_spark_timer);
    s_spark_timer = NULL;
  }
  s_nb = 0;
  s_nb_count = 0;
  for (int i = 0; i < NB_MAX; i++) {
    s_chips[i] = NULL;
    s_chip_name[i] = NULL;
    s_chip_val[i] = NULL;
  }
  s_nb_empty = NULL;

  bool running;
  bool has_rssi;
  int16_t rssi;
  sx1262_stats_t st;
  sample_once(&running, &has_rssi, &rssi, &st);
  s_prev_rx = running ? st.nb_pkt_received : 0;
  uint8_t amp0 = has_rssi ? rssi_to_amp(rssi) : 0;
  for (int i = 0; i < SPARK_PTS; i++) {
    s_ch_buf[i] = amp0;
    s_air_buf[i] = 0;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  lv_obj_t *root = lv_obj_create(s_screen);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(root, ui_screen_w(), BODY_H);
  lv_obj_align(root, LV_ALIGN_TOP_MID, 0, BODY_TOP);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, ROOT_PAD, 0);
  lv_obj_set_style_pad_row(root, ROOT_GAP, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  make_panel(root, "Channel RSSI", lv_color_hex(COL_ACC2), &s_ch_val, &s_ch_line, s_ch_pts);
  make_panel(root, "RX packets", lv_color_hex(COL_CYAN), &s_air_val, &s_air_line, s_air_pts);

  lv_obj_t *batt = lv_obj_create(root);
  lv_obj_remove_flag(batt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(batt, lv_pct(100), BATT_H);
  lv_obj_set_style_bg_opa(batt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(batt, 0, 0);
  lv_obj_set_style_pad_all(batt, 0, 0);
  lv_obj_set_style_pad_column(batt, 7, 0);
  lv_obj_set_flex_flow(batt, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(batt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *bic = lv_label_create(batt);
  lv_label_set_text(bic, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(bic, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(bic, current_theme.text_main, 0);

  s_link_bar = lv_bar_create(batt);
  lv_obj_set_height(s_link_bar, BAR_H);
  lv_obj_set_flex_grow(s_link_bar, 1);
  lv_bar_set_range(s_link_bar, 0, 100);
  lv_bar_set_value(s_link_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_link_bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_link_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_link_bar, BAR_RAD, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_link_bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(s_link_bar, lv_color_hex(COL_ACC2), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(s_link_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_link_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_link_bar, BAR_RAD, LV_PART_INDICATOR);

  s_link_pct = lv_label_create(batt);
  lv_label_set_text(s_link_pct, "--");
  lv_obj_set_style_text_font(s_link_pct, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_link_pct, current_theme.text_main, 0);

  s_link_sub = lv_label_create(batt);
  lv_label_set_text(s_link_sub, "--");
  lv_obj_set_style_text_font(s_link_sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_link_sub, current_theme.text_secondary, 0);

  lv_obj_t *nlbl = lv_label_create(root);
  lv_label_set_text(nlbl, "NEIGHBORS");
  lv_obj_set_style_text_font(nlbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(nlbl, current_theme.text_secondary, 0);

  lv_obj_t *chips = lv_obj_create(root);
  lv_obj_remove_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chips, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(chips, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chips, 0, 0);
  lv_obj_set_style_pad_all(chips, 0, 0);
  lv_obj_set_style_pad_column(chips, CHIP_GAP, 0);
  lv_obj_set_style_pad_row(chips, CHIP_GAP, 0);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  for (int i = 0; i < NB_MAX; i++) {
    lv_obj_t *chip = lv_obj_create(chips);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, CHIP_H);
    lv_obj_set_style_radius(chip, CHIP_RAD, 0);
    lv_obj_set_style_bg_color(chip, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_pad_hor(chip, CHIP_PAD_H, 0);
    lv_obj_set_style_pad_ver(chip, 0, 0);
    lv_obj_set_style_pad_column(chip, 5, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *nm = lv_label_create(chip);
    lv_label_set_text(nm, "node");
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
    s_chip_name[i] = nm;

    lv_obj_t *sv = lv_label_create(chip);
    lv_label_set_text(sv, "--");
    lv_obj_set_style_text_font(sv, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sv, current_theme.text_secondary, 0);
    s_chip_val[i] = sv;

    s_chips[i] = chip;
  }

  s_nb_empty = lv_label_create(chips);
  lv_label_set_text(s_nb_empty, "Listening for nodes...");
  lv_obj_set_style_text_font(s_nb_empty, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_nb_empty, current_theme.text_secondary, 0);
  lv_obj_add_flag(s_nb_empty, LV_OBJ_FLAG_HIDDEN);

  rebuild_points();
  paint_stats(running, has_rssi, rssi, &st);
  refresh_neighbors();

  ui_chrome_footer(s_screen, FOOTER_HINT);

  ui_input_set_screen_handler(lora_telemetry_input, NULL);
  s_spark_timer = lv_timer_create(spark_tick_cb, SPARK_MS, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
