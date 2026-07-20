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

#include "lora_chat_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "st7789.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "LORA_MESH";

#define NAV_TIMER_MS 50
#define CONNECT_MS   5000
#define SIG_GREEN    0x00E676
#define ENTRY_MS     200

#define ICON_BT      "/assets/icons/bluetooth_icon.bin"
#define ICON_RADAR   "/assets/icons/radar_icon.bin"
#define ICON_CONFIG  "/assets/icons/config_icon.bin"
#define ICON_CONNECT "/assets/icons/bluetooth_icon.bin"
#define ICON_NODES   "/assets/icons/node_icon.bin"
#define ART_LORA     "/assets/frames/lora_frame_0.bin"

#define HDR_TITLE_Y    10
#define HDR_TAG_Y      30
#define HDR_RULE_Y     48
#define HDR_RULE_W_PCT 70
#define HDR_RULE_H     2

#define BADGE_SIZE    28
#define BADGE_ICON_PX 18

#define PROTO_CARD_W      210
#define PROTO_CARD_H      66
#define PROTO_CARD_RADIUS 12
#define PROTO_CARD_PAD    10
#define PROTO_CARD_GAP    12
#define PROTO_CARD0_Y     64
#define PROTO_CARD1_Y     138
#define CARD_SHADOW_W     10

#define HOME_ROW_W      210
#define HOME_ROW_H      42
#define HOME_ROW_RADIUS 10
#define HOME_ROW_PAD    9
#define HOME_ROW_GAP    8
#define HOME_ROW0_Y     98
#define HOME_INFO_Y     70

#define RSSI_BAR_GAP 5

#define TOP_BORDER_H 46
#define STATUS_Y_OFS (TOP_BORDER_H + 12)
#define CARD_Y_OFS   86

#define PHASE_STEP_MS 1650
#define PHASE_COUNT   3
#define DOTS_STEP_MS  340
#define DOTS_MAX      3

#define PULSE_DOT_SIZE     7
#define PULSE_DOT_GAP      12
#define PULSE_DOT_COUNT    3
#define PULSE_DOT_MS       320
#define PULSE_DOT_STAGGER  220
#define TYPING_LIFETIME_MS 1200
#define TYPING_PAD         7
#define TYPING_RADIUS      9
#define TYPING_MAX_WIDTH   184

static const char *PROTOS[] = {"MeshCore", "Meshtastic"};
#define PROTO_COUNT ((int)(sizeof(PROTOS) / sizeof(PROTOS[0])))

static const char *PROTO_TAGS[] = {"Encrypted mesh chat", "Long-range telemetry"};
static const char *PROTO_ICONS[] = {ICON_RADAR, ICON_BT};

static const char *HOME_ITEMS[] = {"Connect App", "Nodes", "Configs"};
#define HOME_COUNT ((int)(sizeof(HOME_ITEMS) / sizeof(HOME_ITEMS[0])))

static const char *HOME_TAGS[] = {"Pair companion app", "Mesh peers nearby", "Radio parameters"};
static const char *HOME_ICONS[] = {ICON_CONNECT, ICON_NODES, ICON_CONFIG};

static const char *PHASES[] = {"Scanning mesh", "Handshake", "Authenticating"};

static const struct {
  const char *name;
  int rssi;
  bool strong;
} NODES[] = {
    {"Base Camp", -42, true},
    {"Gateway-1", -55, true},
    {"Relay-7", -78, false},
    {"Drone-A", -67, true},
    {"Trekker", -91, false},
};
#define NODE_COUNT ((int)(sizeof(NODES) / sizeof(NODES[0])))

static const char *OPT_REGION[] = {"US", "EU868", "CN", "ANZ"};
static const char *OPT_CHAN[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
static const char *OPT_PRESET[] = {"LongFast", "MediumFast", "ShortFast", "LongSlow"};
#define OPT_N(a) ((int)(sizeof(a) / sizeof((a)[0])))
enum { CFG_REGION = 0, CFG_CHAN, CFG_PRESET, CFG_POWER, CFG_ROLE, CFG_COUNT };

typedef enum {
  VIEW_PROTO = 0,
  VIEW_HOME,
  VIEW_CONNECT,
  VIEW_NODES,
  VIEW_CHAT,
  VIEW_CONFIGS,
} view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_obj_t *s_chat_list = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_typing_row = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_connect_timer = NULL;
static lv_timer_t *s_reply_timer = NULL;
static lv_timer_t *s_phase_timer = NULL;
static view_t s_view = VIEW_PROTO;
static int s_proto = 0;
static int s_proto_sel = 0;
static int s_home_sel = 0;
static lv_obj_t *s_proto_cards[PROTO_COUNT];
static lv_obj_t *s_home_rows[HOME_COUNT];
static int s_node = 0;
static int s_reply_i = 0;
static int s_phase = 0;
static bool s_linked = false;
static int s_cfg_region = 0, s_cfg_chan = 3, s_cfg_preset = 0;
static int s_msg_clock = 0;

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;

static const char *REPLIES[] = {
    "Roger that.", "Copy.", "On my way.", "Stay safe out there.", "10-4, over."};
#define REPLY_COUNT ((int)(sizeof(REPLIES) / sizeof(REPLIES[0])))

static void nav_timer_cb(lv_timer_t *t);
static void build_screen(void);
static void add_bubble(bool outgoing, const char *who, const char *text);

static void stop_timers(void) {
  if (s_connect_timer != NULL) {
    lv_timer_delete(s_connect_timer);
    s_connect_timer = NULL;
  }
  if (s_reply_timer != NULL) {
    lv_timer_delete(s_reply_timer);
    s_reply_timer = NULL;
  }
  if (s_phase_timer != NULL) {
    lv_timer_delete(s_phase_timer);
    s_phase_timer = NULL;
  }
}

static void anim_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static lv_obj_t *make_icon(lv_obj_t *parent, const char *path, uint32_t scale) {
  lv_image_dsc_t *dsc = assets_get(path);
  if (dsc == NULL)
    return NULL;
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, dsc);
  if (scale != 256)
    lv_image_set_scale(img, scale);
  return img;
}

static lv_obj_t *make_status_pill(lv_obj_t *parent, bool linked) {
  lv_obj_t *pill = lv_obj_create(parent);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, 20);
  lv_obj_set_style_radius(pill, 10, 0);
  lv_obj_set_style_pad_hor(pill, 9, 0);
  lv_obj_set_style_pad_ver(pill, 0, 0);
  lv_obj_set_style_border_width(pill, 1, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  if (linked) {
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x0A3A2E), 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(SIG_GREEN), 0);
  } else {
    lv_obj_set_style_bg_color(pill, current_theme.bg_secondary, 0);
    lv_obj_set_style_border_color(pill, current_theme.border_inactive, 0);
  }
  lv_obj_t *l = lv_label_create(pill);
  lv_label_set_text(l, linked ? LV_SYMBOL_OK "  Linked" : LV_SYMBOL_CLOSE "  Offline");
  lv_obj_set_style_text_color(
      l, linked ? lv_color_hex(SIG_GREEN) : current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_center(l);
  return pill;
}

static void lora_accent_header(const char *title, const char *tagline) {
  lv_obj_t *t = lv_label_create(s_screen);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_color(t, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, HDR_TITLE_Y);

  lv_obj_t *tag = lv_label_create(s_screen);
  lv_label_set_text(tag, tagline);
  lv_obj_set_style_text_color(tag, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
  lv_obj_align(tag, LV_ALIGN_TOP_MID, 0, HDR_TAG_Y);

  lv_obj_t *rule = lv_obj_create(s_screen);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(rule, lv_pct(HDR_RULE_W_PCT), HDR_RULE_H);
  lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, HDR_RULE_Y);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, 1, 0);
  lv_obj_set_style_bg_color(rule, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_40, 0);
}

static lv_obj_t *make_badge_icon(lv_obj_t *parent, const char *path) {
  lv_obj_t *badge = lv_obj_create(parent);
  lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(badge, BADGE_SIZE, BADGE_SIZE);
  lv_obj_set_style_radius(badge, 8, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_set_style_bg_color(badge, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(badge, 1, 0);
  lv_obj_set_style_border_color(badge, current_theme.border_accent, 0);

  lv_image_dsc_t *dsc = assets_get(path);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(badge);
    lv_image_set_src(img, dsc);
    int32_t longest = dsc->header.w > dsc->header.h ? dsc->header.w : dsc->header.h;
    if (longest > 0)
      lv_image_set_scale(img, BADGE_ICON_PX * 256 / longest);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
  }
  return badge;
}

static void style_proto_card(lv_obj_t *card, bool selected) {
  if (selected) {
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(card, CARD_SHADOW_W, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
  } else {
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_TRANSP, 0);
  }
}

static lv_obj_t *make_proto_card(int idx) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, PROTO_CARD_W, PROTO_CARD_H);
  lv_obj_set_style_radius(card, PROTO_CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(card, PROTO_CARD_PAD, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card, PROTO_CARD_PAD, 0);

  make_badge_icon(card, PROTO_ICONS[idx]);

  lv_obj_t *txt = lv_obj_create(card);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(txt, 1);

  lv_obj_t *nm = lv_label_create(txt);
  lv_label_set_text(nm, PROTOS[idx]);
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);

  lv_obj_t *sub = lv_label_create(txt);
  lv_label_set_text(sub, PROTO_TAGS[idx]);
  lv_obj_set_style_text_color(sub, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);

  style_proto_card(card, idx == s_proto_sel);
  return card;
}

static void style_home_row(lv_obj_t *row, int idx, bool selected) {
  bool linked_row = (idx == 0 && s_linked);
  lv_color_t accent = linked_row ? lv_color_hex(SIG_GREEN) : current_theme.border_accent;
  if (selected) {
    lv_obj_set_style_border_width(row, 2, 0);
    lv_obj_set_style_border_color(row, accent, 0);
    lv_obj_set_style_shadow_color(row, accent, 0);
    lv_obj_set_style_shadow_width(row, CARD_SHADOW_W, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_30, 0);
  } else {
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(
        row, linked_row ? lv_color_hex(SIG_GREEN) : current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
  }
}

static lv_obj_t *make_home_row(int idx) {
  bool linked_row = (idx == 0 && s_linked);
  lv_obj_t *row = lv_obj_create(s_screen);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, HOME_ROW_W, HOME_ROW_H);
  lv_obj_set_style_radius(row, HOME_ROW_RADIUS, 0);
  lv_obj_set_style_bg_color(row, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(row, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(row, HOME_ROW_PAD, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, HOME_ROW_PAD, 0);

  make_badge_icon(row, HOME_ICONS[idx]);

  lv_obj_t *txt = lv_obj_create(row);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(txt, 1);

  lv_obj_t *nm = lv_label_create(txt);
  lv_label_set_text(nm, HOME_ITEMS[idx]);
  lv_obj_set_style_text_color(
      nm, linked_row ? lv_color_hex(SIG_GREEN) : current_theme.text_main, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);

  lv_obj_t *sub = lv_label_create(txt);
  lv_label_set_text(sub, linked_row ? "Companion linked" : HOME_TAGS[idx]);
  lv_obj_set_style_text_color(
      sub, linked_row ? lv_color_hex(SIG_GREEN) : current_theme.border_inactive, 0);
  lv_obj_set_style_text_opa(sub, linked_row ? LV_OPA_70 : LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);

  if (idx == 0 && s_linked) {
    lv_obj_t *chk = lv_label_create(row);
    lv_label_set_text(chk, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(chk, lv_color_hex(SIG_GREEN), 0);
    lv_obj_set_style_text_font(chk, &lv_font_montserrat_14, 0);
  }

  style_home_row(row, idx, idx == s_home_sel);
  return row;
}

static void rssi_bars(char *out, size_t n, int rssi) {
  int lvl = 0;
  if (rssi >= -55)
    lvl = 4;
  else if (rssi >= -70)
    lvl = 3;
  else if (rssi >= -85)
    lvl = 2;
  else
    lvl = 1;
  char buf[5] = "....";
  for (int i = 0; i < lvl && i < 4; i++)
    buf[i] = '|';
  buf[4] = '\0';
  snprintf(out, n, "%s", buf);
}

static void build_title(const char *text) {
  lv_obj_t *t = lv_label_create(s_screen);
  lv_label_set_text(t, text);
  lv_obj_set_style_text_color(t, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 12);
}

static void offset_items_below_strip(int strip_h) {
  const int items_y0 = 50;
  int new_y = items_y0 + strip_h;
  int new_h = LCD_V_RES - new_y - 8 - MENU_COMP_FOOTER_H;
  if (new_h < 40)
    new_h = 40;
  lv_obj_set_align(s_menu.items_cont, LV_ALIGN_TOP_LEFT);
  lv_obj_set_y(s_menu.items_cont, new_y);
  lv_obj_set_height(s_menu.items_cont, new_h);
}

static void add_bubble(bool outgoing, const char *who, const char *text) {
  if (s_chat_list == NULL)
    return;
  lv_obj_t *row = lv_obj_create(s_chat_list);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 2, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(row,
                        LV_FLEX_ALIGN_START,
                        outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *b = lv_label_create(row);
  lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_max_width(b, 184, 0);
  lv_obj_set_style_pad_all(b, 7, 0);
  lv_obj_set_style_radius(b, 9, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, outgoing ? lv_color_hex(0x0A6B57) : current_theme.bg_secondary, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(
      b, outgoing ? lv_color_hex(SIG_GREEN) : current_theme.border_interface, 0);
  if (outgoing)
    lv_obj_set_style_radius(b, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(b, current_theme.text_main, 0);
  lv_obj_set_style_text_font(b, &lv_font_montserrat_12, 0);

  if (outgoing) {
    lv_label_set_text(b, text);
  } else {
    char line[160];
    snprintf(line, sizeof(line), "%s\n%s", who, text);
    lv_label_set_text(b, line);
  }

  lv_obj_t *ts = lv_label_create(row);
  lv_label_set_text_fmt(ts, "12:0%d", s_msg_clock % 10);
  s_msg_clock++;
  lv_obj_set_style_text_color(ts, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(ts, &lv_font_montserrat_12, 0);
  lv_obj_set_style_pad_hor(ts, 4, 0);

  lv_obj_scroll_to_view(b, LV_ANIM_ON);
}

static lv_obj_t *add_typing_bubble(const char *who) {
  if (s_chat_list == NULL)
    return NULL;
  lv_obj_t *row = lv_obj_create(s_chat_list);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 2, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *bubble = lv_obj_create(row);
  lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(bubble, TYPING_MAX_WIDTH, 0);
  lv_obj_set_style_pad_all(bubble, TYPING_PAD, 0);
  lv_obj_set_style_radius(bubble, TYPING_RADIUS, 0);
  lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bubble, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_width(bubble, 1, 0);
  lv_obj_set_style_border_color(bubble, current_theme.border_interface, 0);
  lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bubble, RSSI_BAR_GAP, 0);

  lv_obj_t *tag = lv_label_create(bubble);
  lv_label_set_text(tag, who);
  lv_obj_set_style_text_color(tag, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);

  for (int i = 0; i < PULSE_DOT_COUNT; i++) {
    lv_obj_t *dot = lv_obj_create(bubble);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, PULSE_DOT_SIZE, PULSE_DOT_SIZE);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, current_theme.border_accent, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_duration(&a, PULSE_DOT_MS);
    lv_anim_set_playback_duration(&a, PULSE_DOT_MS);
    lv_anim_set_delay(&a, i * PULSE_DOT_STAGGER);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_start(&a);
  }

  lv_obj_scroll_to_view(bubble, LV_ANIM_ON);
  return row;
}

static void reply_cb(lv_timer_t *t) {
  (void)t;
  s_reply_timer = NULL;
  if (lv_screen_active() != s_screen || s_view != VIEW_CHAT)
    return;
  if (s_typing_row != NULL) {
    lv_obj_del(s_typing_row);
    s_typing_row = NULL;
    s_msg_clock--;
  }
  add_bubble(false, NODES[s_node].name, REPLIES[s_reply_i % REPLY_COUNT]);
  s_reply_i++;
}

static void on_kb_submit(const char *text, void *user_data) {
  (void)user_data;
  if (text == NULL || text[0] == '\0' || s_view != VIEW_CHAT)
    return;
  add_bubble(true, NULL, text);
  if (s_reply_timer != NULL)
    lv_timer_delete(s_reply_timer);
  if (s_typing_row != NULL) {
    lv_obj_del(s_typing_row);
    s_msg_clock--;
  }
  s_typing_row = add_typing_bubble(NODES[s_node].name);
  s_reply_timer = lv_timer_create(reply_cb, TYPING_LIFETIME_MS, NULL);
  lv_timer_set_repeat_count(s_reply_timer, 1);
}

static void status_dots_cb(void *var, int32_t v) {
  lv_obj_t *label = (lv_obj_t *)var;
  static const char *DOTS[] = {"", ".", "..", "..."};
  int n = (int)v;
  if (n < 0)
    n = 0;
  if (n > DOTS_MAX)
    n = DOTS_MAX;
  char buf[40];
  snprintf(buf, sizeof(buf), "%s%s", PHASES[s_phase], DOTS[n]);
  lv_label_set_text(label, buf);
}

static void phase_step_cb(lv_timer_t *t) {
  (void)t;
  if (lv_screen_active() != s_screen || s_view != VIEW_CONNECT) {
    if (s_phase_timer != NULL) {
      lv_timer_delete(s_phase_timer);
      s_phase_timer = NULL;
    }
    return;
  }
  if (s_phase < PHASE_COUNT - 1)
    s_phase++;
  if (s_phase >= PHASE_COUNT - 1 && s_phase_timer != NULL) {
    lv_timer_delete(s_phase_timer);
    s_phase_timer = NULL;
  }
}

static void connect_done_cb(lv_timer_t *t) {
  (void)t;
  s_connect_timer = NULL;
  if (lv_screen_active() != s_screen || s_view != VIEW_CONNECT)
    return;
  s_linked = true;
  build_screen();
}

static lv_obj_t *make_device_card(lv_obj_t *parent, bool linked) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, 196, 60);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(
      card, linked ? lv_color_hex(SIG_GREEN) : current_theme.border_interface, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card, 8, 0);

  lv_obj_t *ic = make_icon(card, ICON_BT, 256);
  if (ic == NULL) {
    lv_obj_t *g = lv_label_create(card);
    lv_label_set_text(g, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(g, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(g, &lv_font_montserrat_16, 0);
  }

  lv_obj_t *txt = lv_obj_create(card);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(txt, 1);

  lv_obj_t *nm = lv_label_create(txt);
  lv_label_set_text(nm, "HighBoy Companion");
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);

  lv_obj_t *sub = lv_label_create(txt);
  lv_label_set_text(sub, "v1.2  ·  BLE");
  lv_obj_set_style_text_color(sub, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  return card;
}

static void build_connect(void) {
  ui_chrome_header(s_screen, PROTOS[s_proto], ICON_RADAR);

  if (!s_linked) {
    s_phase = 0;
    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, PHASES[s_phase]);
    lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_Y_OFS);

    lv_anim_t ad;
    lv_anim_init(&ad);
    lv_anim_set_var(&ad, s_status_label);
    lv_anim_set_exec_cb(&ad, status_dots_cb);
    lv_anim_set_values(&ad, 0, DOTS_MAX);
    lv_anim_set_duration(&ad, DOTS_STEP_MS * DOTS_MAX);
    lv_anim_set_repeat_count(&ad, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&ad);

    waves_create(s_screen, LV_ALIGN_CENTER, 0, -4, LV_SYMBOL_BLUETOOTH, ICON_BT);

    lv_obj_t *card = make_device_card(s_screen, false);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);

    s_phase_timer = lv_timer_create(phase_step_cb, PHASE_STEP_MS, NULL);

    s_connect_timer = lv_timer_create(connect_done_cb, CONNECT_MS, NULL);
    lv_timer_set_repeat_count(s_connect_timer, 1);
  } else {
    lv_obj_t *node = lv_obj_create(s_screen);
    lv_obj_remove_flag(node, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(node, 64, 64);
    lv_obj_align(node, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_radius(node, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(node, lv_color_hex(SIG_GREEN), 0);
    lv_obj_set_style_bg_opa(node, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(node, 0, 0);
    lv_obj_t *chk = lv_label_create(node);
    lv_label_set_text(chk, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(chk, lv_color_hex(0x05130E), 0);
    lv_obj_set_style_text_font(chk, &lv_font_montserrat_16, 0);
    lv_obj_center(chk);

    lv_obj_t *st = lv_label_create(s_screen);
    lv_label_set_text(st, "Companion linked!");
    lv_obj_set_style_text_color(st, lv_color_hex(SIG_GREEN), 0);
    lv_obj_set_style_text_font(st, &lv_font_montserrat_14, 0);
    lv_obj_align(st, LV_ALIGN_CENTER, 0, 8);

    lv_obj_t *card = make_device_card(s_screen, true);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 56);
  }

  ui_chrome_footer(s_screen, "BACK to exit");
}

static void build_chat(void) {
  s_msg_clock = 0;

  lv_obj_t *hdr = lv_obj_create(s_screen);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr, LV_PCT(100), 28);
  lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(hdr, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(hdr, 1, 0);
  lv_obj_set_style_border_color(hdr, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(hdr, 0, 0);

  lv_obj_t *dot = lv_obj_create(hdr);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dot, 8, 8);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(SIG_GREEN), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_align(dot, LV_ALIGN_LEFT_MID, 8, 0);

  lv_obj_t *nm = lv_label_create(hdr);
  lv_label_set_text_fmt(nm, LV_SYMBOL_LEFT "  %s", NODES[s_node].name);
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
  lv_obj_align(nm, LV_ALIGN_LEFT_MID, 22, 0);

  lv_obj_t *sig = lv_label_create(hdr);
  lv_label_set_text_fmt(sig, "%ddBm", NODES[s_node].rssi);
  lv_obj_set_style_text_color(
      sig, NODES[s_node].strong ? lv_color_hex(SIG_GREEN) : current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(sig, &lv_font_montserrat_12, 0);
  lv_obj_align(sig, LV_ALIGN_RIGHT_MID, -8, 0);

  s_chat_list = lv_obj_create(s_screen);
  lv_obj_set_size(s_chat_list, LV_PCT(100), LV_PCT(70));
  lv_obj_align(s_chat_list, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_bg_opa(s_chat_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_chat_list, 0, 0);
  lv_obj_set_style_pad_all(s_chat_list, 4, 0);
  lv_obj_set_flex_flow(s_chat_list, LV_FLEX_FLOW_COLUMN);

  add_bubble(false, NODES[s_node].name, "Hey, you on the mesh?");
  add_bubble(true, NULL, "Yep, reading you 5/5.");
  add_bubble(false, NODES[s_node].name, "Signal's solid here.");

  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text(hint, LV_SYMBOL_KEYBOARD " OK write   BACK nodes");
  lv_obj_set_style_text_color(hint, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

static void build_proto_view(void) {
  s_proto_sel = s_proto;
  lora_accent_header("LORA MESH", "Select a network");
  for (int i = 0; i < PROTO_COUNT; i++) {
    s_proto_cards[i] = make_proto_card(i);
    lv_obj_align(s_proto_cards[i], LV_ALIGN_TOP_MID, 0, i == 0 ? PROTO_CARD0_Y : PROTO_CARD1_Y);
  }

  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text(hint, LV_SYMBOL_UP LV_SYMBOL_DOWN " choose   OK enter   BACK exit");
  lv_obj_set_style_text_color(hint, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void build_home_view(void) {
  lora_accent_header(PROTOS[s_proto], s_linked ? "Mesh online" : "Standalone");

  lv_obj_t *pill = make_status_pill(s_screen, s_linked);
  lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -10, 8);

  lv_obj_t *info = lv_label_create(s_screen);
  lv_label_set_text_fmt(info,
                        LV_SYMBOL_GPS "  CH %s  ·  %s  ·  %s",
                        OPT_CHAN[s_cfg_chan],
                        OPT_REGION[s_cfg_region],
                        OPT_PRESET[s_cfg_preset]);
  lv_obj_set_style_text_color(info, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(info, LV_OPA_70, 0);
  lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
  lv_obj_align(info, LV_ALIGN_TOP_MID, 0, HOME_INFO_Y);

  for (int i = 0; i < HOME_COUNT; i++) {
    s_home_rows[i] = make_home_row(i);
    lv_obj_align(
        s_home_rows[i], LV_ALIGN_TOP_MID, 0, HOME_ROW0_Y + i * (HOME_ROW_H + HOME_ROW_GAP));
  }

  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text(hint, LV_SYMBOL_UP LV_SYMBOL_DOWN " move   OK open   BACK protocols");
  lv_obj_set_style_text_color(hint, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void build_screen(void) {
  stop_timers();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_chat_list = NULL;
  s_status_label = NULL;
  s_typing_row = NULL;
  for (int i = 0; i < PROTO_COUNT; i++)
    s_proto_cards[i] = NULL;
  for (int i = 0; i < HOME_COUNT; i++)
    s_home_rows[i] = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *fade_target = NULL;

  if (s_view == VIEW_PROTO) {
    build_proto_view();
    fade_target = s_screen;
  } else if (s_view == VIEW_HOME) {
    build_home_view();
    fade_target = s_screen;
  } else if (s_view == VIEW_NODES) {
    s_menu = menu_component_create(s_screen, "NODES", "/assets/icons/node_icon.bin");
    for (int i = 0; i < NODE_COUNT; i++) {
      char bars[8];
      char row[48];
      rssi_bars(bars, sizeof(bars), NODES[i].rssi);
      snprintf(row, sizeof(row), "%s   %s %ddBm", NODES[i].name, bars, NODES[i].rssi);
      menu_component_add_item(&s_menu, ICON_NODES, row);
      if (NODES[i].strong)
        menu_component_set_item_label_color(&s_menu, i, lv_color_hex(SIG_GREEN));
    }
    int online = 0;
    for (int i = 0; i < NODE_COUNT; i++)
      if (NODES[i].strong)
        online++;
    lv_obj_t *note = lv_label_create(s_screen);
    lv_label_set_text_fmt(note, LV_SYMBOL_GPS "  %d / %d nodes online", online, NODE_COUNT);
    lv_obj_set_style_text_color(note, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0);
    lv_obj_set_align(note, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(note, 12, 52);
    offset_items_below_strip(24);
    fade_target = s_menu.items_cont;
  } else if (s_view == VIEW_CONFIGS) {
    s_menu = menu_component_create(s_screen, "CONFIGS", "/assets/icons/config_icon.bin");
    menu_component_add_selector(&s_menu, ICON_CONFIG, "Region", OPT_REGION[s_cfg_region]);
    menu_component_add_selector(&s_menu, ICON_CONFIG, "Channel", OPT_CHAN[s_cfg_chan]);
    menu_component_add_selector(&s_menu, ICON_CONFIG, "Preset", OPT_PRESET[s_cfg_preset]);
    menu_component_add_intensity(&s_menu, ICON_CONFIG, "TX Power", 4);
    menu_component_add_toggle(&s_menu, ICON_CONFIG, "Router mode", false);
    lv_obj_t *note = lv_label_create(s_screen);
    lv_label_set_text(note, LV_SYMBOL_SETTINGS "  Radio settings");
    lv_obj_set_style_text_color(note, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_12, 0);
    lv_obj_set_align(note, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(note, 12, 52);
    offset_items_below_strip(24);
    fade_target = s_menu.items_cont;
  } else if (s_view == VIEW_CONNECT) {
    build_connect();
    fade_target = s_screen;
  } else {
    build_chat();
    fade_target = s_chat_list;
  }

  if (fade_target)
    lv_obj_fade_in(fade_target, ENTRY_MS, 0);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void cycle_config(int sel, int dir) {
  if (sel == CFG_REGION) {
    s_cfg_region = (s_cfg_region + dir + OPT_N(OPT_REGION)) % OPT_N(OPT_REGION);
    menu_component_set_selector_value(&s_menu, sel, OPT_REGION[s_cfg_region]);
  } else if (sel == CFG_CHAN) {
    s_cfg_chan = (s_cfg_chan + dir + OPT_N(OPT_CHAN)) % OPT_N(OPT_CHAN);
    menu_component_set_selector_value(&s_menu, sel, OPT_CHAN[s_cfg_chan]);
  } else if (sel == CFG_PRESET) {
    s_cfg_preset = (s_cfg_preset + dir + OPT_N(OPT_PRESET)) % OPT_N(OPT_PRESET);
    menu_component_set_selector_value(&s_menu, sel, OPT_PRESET[s_cfg_preset]);
  } else if (sel == CFG_POWER) {
    if (dir > 0)
      menu_component_intensity_inc(&s_menu, sel);
    else
      menu_component_intensity_dec(&s_menu, sel);
  }
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    stop_timers();
    return;
  }
  if (ui_input_is_locked() || keyboard_is_open())
    return;

  bool up = ui_btn_up(), down = ui_btn_down();
  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  switch (s_view) {
    case VIEW_PROTO:
      if (down && !s_down_last && s_proto_sel < PROTO_COUNT - 1) {
        s_proto_sel++;
        style_proto_card(s_proto_cards[s_proto_sel - 1], false);
        style_proto_card(s_proto_cards[s_proto_sel], true);
      }
      if (up && !s_up_last && s_proto_sel > 0) {
        s_proto_sel--;
        style_proto_card(s_proto_cards[s_proto_sel + 1], false);
        style_proto_card(s_proto_cards[s_proto_sel], true);
      }
      if (ok && !s_ok_last) {
        s_proto = s_proto_sel;
        s_view = VIEW_HOME;
        s_home_sel = 0;
        build_screen();
        goto edges;
      }
      if (back && !s_back_last)
        ui_switch_screen(SCREEN_MENU);
      break;

    case VIEW_HOME:
      if (down && !s_down_last && s_home_sel < HOME_COUNT - 1) {
        s_home_sel++;
        style_home_row(s_home_rows[s_home_sel - 1], s_home_sel - 1, false);
        style_home_row(s_home_rows[s_home_sel], s_home_sel, true);
      }
      if (up && !s_up_last && s_home_sel > 0) {
        s_home_sel--;
        style_home_row(s_home_rows[s_home_sel + 1], s_home_sel + 1, false);
        style_home_row(s_home_rows[s_home_sel], s_home_sel, true);
      }
      if (ok && !s_ok_last) {
        s_view = (s_home_sel == 0) ? VIEW_CONNECT : (s_home_sel == 1) ? VIEW_NODES : VIEW_CONFIGS;
        build_screen();
        goto edges;
      }
      if (back && !s_back_last) {
        s_view = VIEW_PROTO;
        build_screen();
        goto edges;
      }
      break;

    case VIEW_CONNECT:
      if (back && !s_back_last) {
        s_view = VIEW_HOME;
        build_screen();
        goto edges;
      }
      break;

    case VIEW_NODES:
      if (down && !s_down_last)
        menu_component_next(&s_menu);
      if (up && !s_up_last)
        menu_component_prev(&s_menu);
      if (ok && !s_ok_last) {
        s_node = menu_component_get_selected(&s_menu);
        s_view = VIEW_CHAT;
        build_screen();
        goto edges;
      }
      if (back && !s_back_last) {
        s_view = VIEW_HOME;
        build_screen();
        goto edges;
      }
      break;

    case VIEW_CONFIGS: {
      int sel = menu_component_get_selected(&s_menu);
      if (down && !s_down_last)
        menu_component_next(&s_menu);
      if (up && !s_up_last)
        menu_component_prev(&s_menu);
      if (left && !s_left_last)
        cycle_config(sel, -1);
      if (right && !s_right_last)
        cycle_config(sel, +1);
      if (ok && !s_ok_last && sel == CFG_ROLE)
        menu_component_toggle_item(&s_menu, sel);
      if (back && !s_back_last) {
        s_view = VIEW_HOME;
        build_screen();
        goto edges;
      }
      break;
    }

    case VIEW_CHAT:
      if (down && !s_down_last && s_chat_list)
        lv_obj_scroll_by(s_chat_list, 0, -36, LV_ANIM_ON);
      if (up && !s_up_last && s_chat_list)
        lv_obj_scroll_by(s_chat_list, 0, 36, LV_ANIM_ON);
      if (ok && !s_ok_last)
        keyboard_open(NULL, on_kb_submit, NULL);
      if (back && !s_back_last) {
        s_view = VIEW_NODES;
        build_screen();
        goto edges;
      }
      break;
  }

edges:
  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_lora_chat_open(void) {
  s_view = VIEW_PROTO;
  s_proto = 0;
  s_node = 0;
  s_linked = false;
  s_phase = 0;
  s_connect_timer = NULL;
  s_reply_timer = NULL;
  s_phase_timer = NULL;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  build_screen();
  ESP_LOGI(TAG, "LoRa mesh (mock) opened");
}

void ui_lora_chat_open_chat(void) {
  s_view = VIEW_CHAT;
  s_proto = 0;
  s_node = 0;
  s_linked = true;
  s_phase = 0;
  s_connect_timer = NULL;
  s_reply_timer = NULL;
  s_phase_timer = NULL;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  build_screen();
  ESP_LOGI(TAG, "LoRa chat (mock) opened at chat view");
}
