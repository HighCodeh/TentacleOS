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

#include "lora_traceroute_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "lora_session.h"
#include "meshtastic_nodedb.h"
#include "mt_mod_traceroute.h"
#include "st7789.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "LORA_TRACERT";

#define POLL_MS       500
#define TIMEOUT_TICKS 30

#define ENTRY_MS       220
#define ROW_STAGGER_MS 90

#define BODY_TOP_Y  46
#define HOP_LIST_W  228
#define HOP_ROW_W   214
#define HOP_ROW_H   46
#define HOP_ROW_GAP 8
#define HOP_RADIUS  12
#define HOP_PAD     9

#define PANEL_SHADOW_W      14
#define PANEL_SHADOW_SPREAD (-3)

#define IDX_BADGE  26
#define IDX_RADIUS 8

#define ICON_HUB     "/assets/icons/hub.bin"
#define WAVES_Y_OFS  (-10)
#define STATUS_Y_OFS 96
#define RISE_PX      24

#define PICK_MAX 64

typedef enum {
  TR_UNSUPPORTED = 0,
  TR_PICKER,
  TR_TRACING,
  TR_RESULT,
  TR_TIMEOUT,
} tr_view_t;

typedef struct {
  uint32_t num;
  char name[MT_NODEDB_LONG_NAME_LEN];
} pick_entry_t;

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_poll_timer = NULL;
static tr_view_t s_view = TR_PICKER;
static int s_poll_ticks = 0;

static pick_entry_t s_picks[PICK_MAX];
static int s_pick_count = 0;
static int s_pick_sel = 0;
static lv_obj_t *s_pick_list = NULL;
static lv_obj_t *s_pick_rows[PICK_MAX];
static lv_obj_t *s_pick_names[PICK_MAX];

static uint32_t s_target_num = 0;
static char s_target_name[MT_NODEDB_LONG_NAME_LEN];

static uint32_t s_hops[MT_TRACE_MAX_HOPS];
static int s_hop_count = 0;

static void lora_traceroute_input(const input_event_t *ev, void *ctx);
static void build_screen(void);

static void stop_poll_timer(void) {
  if (s_poll_timer != NULL) {
    lv_timer_delete(s_poll_timer);
    s_poll_timer = NULL;
  }
}

static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void transy_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void reveal_row(lv_obj_t *o, int idx) {
  if (o == NULL)
    return;
  lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);

  lv_anim_t ao;
  lv_anim_init(&ao);
  lv_anim_set_var(&ao, o);
  lv_anim_set_exec_cb(&ao, opa_cb);
  lv_anim_set_values(&ao, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&ao, ENTRY_MS);
  lv_anim_set_delay(&ao, idx * ROW_STAGGER_MS);
  lv_anim_start(&ao);

  lv_anim_t ay;
  lv_anim_init(&ay);
  lv_anim_set_var(&ay, o);
  lv_anim_set_exec_cb(&ay, transy_cb);
  lv_anim_set_values(&ay, RISE_PX, 0);
  lv_anim_set_duration(&ay, ENTRY_MS);
  lv_anim_set_delay(&ay, idx * ROW_STAGGER_MS);
  lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
  lv_anim_start(&ay);
}

static const char *best_name(const mt_node_entry_t *e) {
  if (e == NULL)
    return NULL;
  if (e->long_name[0] != '\0')
    return e->long_name;
  if (e->short_name[0] != '\0')
    return e->short_name;
  if (e->id[0] != '\0')
    return e->id;
  return NULL;
}

static void resolve_hop_name(uint32_t num, char *buf, size_t len) {
  const char *nm = best_name(mt_nodedb_get(num));
  if (nm != NULL)
    snprintf(buf, len, "%s", nm);
  else
    snprintf(buf, len, "!%08lx", (unsigned long)num);
}

static const char *hop_role(int idx, int count) {
  if (idx >= count - 1)
    return "target";
  if (idx == 0)
    return "origin";
  return "relay";
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h, lv_color_t accent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, HOP_RADIUS, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, accent, 0);
  lv_obj_set_style_shadow_color(p, accent, 0);
  lv_obj_set_style_shadow_width(p, PANEL_SHADOW_W, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, PANEL_SHADOW_SPREAD, 0);
  return p;
}

static lv_obj_t *make_index_badge(lv_obj_t *parent, int number, lv_color_t accent) {
  lv_obj_t *badge = lv_obj_create(parent);
  lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(badge, IDX_BADGE, IDX_BADGE);
  lv_obj_set_style_radius(badge, IDX_RADIUS, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_set_style_bg_color(badge, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(badge, 1, 0);
  lv_obj_set_style_border_color(badge, accent, 0);

  lv_obj_t *l = lv_label_create(badge);
  lv_label_set_text_fmt(l, "%d", number);
  lv_obj_set_style_text_color(l, accent, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_center(l);
  return badge;
}

static void make_hop_row(lv_obj_t *parent, int idx, int count, lv_color_t accent) {
  lv_obj_t *row = lit_panel(parent, HOP_ROW_W, HOP_ROW_H, accent);
  lv_obj_set_style_pad_all(row, HOP_PAD, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 9, 0);

  make_index_badge(row, idx + 1, accent);

  lv_obj_t *txt = lv_obj_create(row);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(txt, 1);

  lv_obj_t *nm = lv_label_create(txt);
  char name[40];
  resolve_hop_name(s_hops[idx], name, sizeof(name));
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_set_width(nm, HOP_ROW_W - IDX_BADGE - (HOP_PAD * 2) - 18);
  lv_label_set_text(nm, name);
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);

  lv_obj_t *sub = lv_label_create(txt);
  lv_label_set_text(sub, hop_role(idx, count));
  lv_obj_set_style_text_color(sub, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
}

static void collect_nodes(void) {
  s_pick_count = 0;
  uint16_t total = mt_nodedb_count();
  for (uint16_t i = 0; i < total && s_pick_count < PICK_MAX; i++) {
    const mt_node_entry_t *e = mt_nodedb_get_by_index(i);
    if (e == NULL || !e->in_use)
      continue;
    const char *nm = best_name(e);
    if (nm != NULL)
      snprintf(s_picks[s_pick_count].name, sizeof(s_picks[s_pick_count].name), "%s", nm);
    else
      snprintf(s_picks[s_pick_count].name,
               sizeof(s_picks[s_pick_count].name),
               "!%08lx",
               (unsigned long)e->num);
    s_picks[s_pick_count].num = e->num;
    s_pick_count++;
  }
  if (s_pick_sel >= s_pick_count)
    s_pick_sel = (s_pick_count > 0) ? s_pick_count - 1 : 0;
  if (s_pick_sel < 0)
    s_pick_sel = 0;
}

static void pick_style_row(int i, bool selected) {
  if (i < 0 || i >= s_pick_count)
    return;
  lv_obj_t *row = s_pick_rows[i];
  if (row == NULL)
    return;
  lv_color_t accent = ui_theme_get_accent();
  lv_obj_set_style_border_color(row, selected ? accent : current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  if (selected) {
    lv_obj_set_style_shadow_color(row, accent, 0);
    lv_obj_set_style_shadow_width(row, PANEL_SHADOW_W, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(row, PANEL_SHADOW_SPREAD, 0);
  } else {
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
  }
  if (s_pick_names[i] != NULL)
    lv_obj_set_style_text_color(
        s_pick_names[i], selected ? current_theme.text_main : current_theme.text_secondary, 0);
}

static void pick_select(int sel) {
  for (int i = 0; i < s_pick_count; i++)
    pick_style_row(i, i == sel);
  if (s_pick_list != NULL && sel >= 0 && sel < s_pick_count && s_pick_rows[sel] != NULL) {
    lv_obj_update_layout(s_pick_list);
    lv_obj_scroll_to_view(s_pick_rows[sel], LV_ANIM_ON);
  }
}

static void centered_note(const char *text) {
  lv_obj_t *msg = lv_label_create(s_screen);
  lv_label_set_text(msg, text);
  lv_obj_set_style_text_color(msg, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
  lv_obj_center(msg);
}

static void build_unsupported(void) {
  ui_chrome_header(s_screen, "TRACEROUTE", ICON_HUB);
  centered_note("Meshtastic only");
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT " BACK to LoRa");
}

static void build_picker(void) {
  ui_chrome_header(s_screen, "TRACEROUTE", ICON_HUB);
  collect_nodes();

  if (s_pick_count == 0) {
    centered_note("No nodes discovered");
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT " BACK to LoRa");
    return;
  }

  s_pick_list = lv_obj_create(s_screen);
  lv_obj_set_size(s_pick_list, ui_screen_w(), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_pick_list, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_pick_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_pick_list, 0, 0);
  lv_obj_set_style_pad_all(s_pick_list, 0, 0);
  lv_obj_set_style_pad_top(s_pick_list, 6, 0);
  lv_obj_set_style_pad_bottom(s_pick_list, 6, 0);
  lv_obj_set_style_pad_row(s_pick_list, HOP_ROW_GAP, 0);
  lv_obj_set_flex_flow(s_pick_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      s_pick_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(s_pick_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_pick_list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(s_pick_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(s_pick_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

  lv_color_t accent = ui_theme_get_accent();
  for (int i = 0; i < s_pick_count; i++) {
    lv_obj_t *row = lit_panel(s_pick_list, HOP_ROW_W, HOP_ROW_H, current_theme.border_inactive);
    lv_obj_set_style_pad_all(row, HOP_PAD, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 9, 0);

    make_index_badge(row, i + 1, accent);

    lv_obj_t *nm = lv_label_create(row);
    lv_obj_set_flex_grow(nm, 1);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_label_set_text(nm, s_picks[i].name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);

    s_pick_rows[i] = row;
    s_pick_names[i] = nm;
  }

  pick_select(s_pick_sel);
  ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " pick  OK trace  BACK LoRa");
}

static void build_tracing(void) {
  ui_chrome_header(s_screen, "TRACEROUTE", ICON_HUB);

  waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y_OFS, LV_SYMBOL_GPS, ICON_HUB);

  lv_obj_t *status = lv_label_create(s_screen);
  char buf[64];
  snprintf(buf, sizeof(buf), "Tracing to %s...", s_target_name);
  lv_label_set_text(status, buf);
  lv_obj_set_style_text_color(status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, STATUS_Y_OFS);

  ui_chrome_footer(s_screen, "BACK to cancel");
}

static void build_result(void) {
  ui_chrome_header(s_screen, "TRACEROUTE", ICON_HUB);

  int n = s_hop_count;
  if (n < 0)
    n = 0;
  if (n > MT_TRACE_MAX_HOPS)
    n = MT_TRACE_MAX_HOPS;

  if (n == 0) {
    centered_note("Empty route");
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT " BACK to LoRa");
    return;
  }

  lv_obj_t *list = lv_obj_create(s_screen);
  lv_obj_set_size(list, HOP_LIST_W, ui_screen_h() - BODY_TOP_Y - UI_CHROME_FOOTER_H);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, BODY_TOP_Y);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, HOP_ROW_GAP, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);

  lv_color_t accent = ui_theme_get_accent();
  for (int i = 0; i < n; i++)
    make_hop_row(list, i, n, accent);
  for (int i = 0; i < n; i++)
    reveal_row(lv_obj_get_child(list, i), i);

  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT " BACK to LoRa");
}

static void build_timeout(void) {
  ui_chrome_header(s_screen, "TRACEROUTE", ICON_HUB);
  centered_note("No response");
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT " BACK to LoRa");
}

static void trace_poll_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen || s_view != TR_TRACING) {
    lv_timer_delete(t);
    s_poll_timer = NULL;
    return;
  }

  uint32_t hops[MT_TRACE_MAX_HOPS];
  int n = 0;
  uint32_t target = 0;
  if (mt_mod_traceroute_get_result(hops, &n, &target)) {
    if (n < 0)
      n = 0;
    if (n > MT_TRACE_MAX_HOPS)
      n = MT_TRACE_MAX_HOPS;
    for (int i = 0; i < n; i++)
      s_hops[i] = hops[i];
    s_hop_count = n;
    s_view = TR_RESULT;
    ui_feedback(UI_FB_READ);
    build_screen();
    return;
  }

  if (++s_poll_ticks >= TIMEOUT_TICKS) {
    s_view = TR_TIMEOUT;
    build_screen();
  }
}

static void build_screen(void) {
  stop_poll_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_pick_list = NULL;
  for (int i = 0; i < PICK_MAX; i++) {
    s_pick_rows[i] = NULL;
    s_pick_names[i] = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  switch (s_view) {
    case TR_UNSUPPORTED:
      build_unsupported();
      break;
    case TR_TRACING:
      build_tracing();
      s_poll_ticks = 0;
      s_poll_timer = lv_timer_create(trace_poll_cb, POLL_MS, NULL);
      break;
    case TR_RESULT:
      build_result();
      break;
    case TR_TIMEOUT:
      build_timeout();
      break;
    case TR_PICKER:
    default:
      build_picker();
      break;
  }

  ui_input_set_screen_handler(lora_traceroute_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void lora_traceroute_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if ((ev->button == INPUT_BTN_BACK || ev->button == INPUT_BTN_LEFT) && press) {
    ui_switch_screen(SCREEN_LORA_CHAT);
    return;
  }

  if (s_view != TR_PICKER || s_pick_count <= 0)
    return;

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav) {
        s_pick_sel = (s_pick_sel + 1) % s_pick_count;
        pick_select(s_pick_sel);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_pick_sel = (s_pick_sel - 1 + s_pick_count) % s_pick_count;
        pick_select(s_pick_sel);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        if (s_pick_sel < 0 || s_pick_sel >= s_pick_count)
          break;
        s_target_num = s_picks[s_pick_sel].num;
        snprintf(s_target_name, sizeof(s_target_name), "%s", s_picks[s_pick_sel].name);
        ui_feedback(UI_FB_SELECT);
        mt_mod_traceroute_start(s_target_num);
        s_view = TR_TRACING;
        build_screen();
      }
      break;
    default:
      break;
  }
}

void ui_lora_traceroute_open(void) {
  ui_theme_set_protocol(PROTOCOL_LORA);

  s_poll_timer = NULL;
  s_poll_ticks = 0;
  s_pick_count = 0;
  s_pick_sel = 0;
  s_pick_list = NULL;
  s_hop_count = 0;
  s_target_num = 0;
  s_target_name[0] = '\0';
  for (int i = 0; i < PICK_MAX; i++) {
    s_pick_rows[i] = NULL;
    s_pick_names[i] = NULL;
  }

  lora_proto_t proto = lora_session_active();
  s_view = (proto == LORA_PROTO_MESHTASTIC) ? TR_PICKER : TR_UNSUPPORTED;

  build_screen();
  ESP_LOGI(TAG, "LoRa traceroute opened (proto=%d)", (int)proto);
}
