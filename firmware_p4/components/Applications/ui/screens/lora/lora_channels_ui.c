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

#include "lora_channels_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_random.h"

#include "lvgl.h"
#include "st7789.h"

#include "keyboard_ui.h"
#include "lora_session.h"
#include "meshcore.h"
#include "meshtastic_channels.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define HDR_TITLE   "CHANNELS"
#define HDR_ICON    NULL
#define FOOTER_HINT "UP/DN  OK RENAME  RIGHT OFF  BACK"

#define BODY_TOP UI_CHROME_HEADER_H
#define BODY_H   (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)

#define GRID_PAD 7
#define GRID_GAP 6

#define TILE_W   110
#define TILE_H   80
#define TILE_PAD 6
#define TILE_GAP 2
#define TILE_RAD 9

#define DOT_SZ  6
#define KEY_SZ  8
#define KEY_RAD 2

#define BAR_CNT   8
#define BAR_W     3
#define BAR_GAP   2
#define BAR_MAX_H 14

#define GLOW_W   12
#define GLOW_SPR -2

#define TILE_CNT 8

#define KEY_LEN  16
#define BAR_BASE 35
#define BAR_SPAN 66

#define COL_ACC2 0xB89AFF
#define COL_CYAN 0x37E0A8
#define COL_DIM  0x8A8594
#define COL_SLOT 0x4A4556

typedef struct {
  bool used;
  bool primary;
  char name[32];
  char role[16];
  uint8_t bars[BAR_CNT];
} ch_slot_t;

static ch_slot_t s_slot[TILE_CNT];

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_grid = NULL;
static lv_obj_t *s_tiles[TILE_CNT];
static lv_obj_t *s_empty_msg = NULL;

static lora_proto_t s_proto = LORA_PROTO_NONE;
static int s_sel = 0;

static void fill_bars(uint8_t *bars, const uint8_t *src, int len) {
  for (int b = 0; b < BAR_CNT; b++) {
    if (src == NULL || len <= 0) {
      bars[b] = BAR_BASE;
      continue;
    }
    uint8_t a = src[b % len];
    uint8_t c = src[(len - 1 - b + len) % len];
    bars[b] = (uint8_t)(BAR_BASE + ((a ^ c) % BAR_SPAN));
  }
}

static void load_slots(void) {
  memset(s_slot, 0, sizeof(s_slot));
  s_proto = lora_session_active();

  if (s_proto == LORA_PROTO_MESHTASTIC) {
    for (int i = 0; i < TILE_CNT; i++) {
      const mt_channel_t *ch = mt_channel_get((uint8_t)i);
      if (ch == NULL || !ch->is_used || ch->role == MT_CH_DISABLED)
        continue;
      s_slot[i].used = true;
      s_slot[i].primary = (ch->role == MT_CH_PRIMARY);
      snprintf(s_slot[i].name,
               sizeof(s_slot[i].name),
               "%s",
               (ch->name[0] != '\0') ? ch->name : "channel");
      snprintf(s_slot[i].role,
               sizeof(s_slot[i].role),
               "%s",
               (ch->role == MT_CH_PRIMARY) ? "PRIMARY" : "SECONDARY");
      fill_bars(s_slot[i].bars, ch->psk, MT_PSK_SIZE);
    }
  } else if (s_proto == LORA_PROTO_MESHCORE) {
    for (int i = 0; i < TILE_CNT; i++) {
      const meshcore_channel_t *ch = meshcore_channel_get((uint8_t)i);
      if (ch == NULL || !ch->is_used)
        continue;
      s_slot[i].used = true;
      s_slot[i].primary = (i == MESHCORE_PUBLIC_CHANNEL);
      snprintf(s_slot[i].name,
               sizeof(s_slot[i].name),
               "%s",
               (ch->name[0] != '\0') ? ch->name : "channel");
      snprintf(s_slot[i].role,
               sizeof(s_slot[i].role),
               "%s",
               (i == MESHCORE_PUBLIC_CHANNEL) ? "PUBLIC" : "GROUP");
      fill_bars(s_slot[i].bars, ch->secret, KEY_LEN);
    }
  }
}

static lv_obj_t *bare_box(lv_obj_t *parent, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

static void build_filled_tile(lv_obj_t *tile, int i) {
  bool primary = s_slot[i].primary;
  lv_color_t sig = primary ? current_theme.border_accent : lv_color_hex(COL_CYAN);
  lv_color_t dot = primary ? lv_color_hex(COL_ACC2) : lv_color_hex(COL_CYAN);

  lv_obj_t *head = bare_box(tile, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 5, 0);

  lv_obj_t *idx = lv_label_create(head);
  lv_label_set_text_fmt(idx, "%d", i);
  lv_obj_set_style_text_font(idx, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(
      idx, primary ? current_theme.border_accent : lv_color_hex(COL_DIM), 0);

  lv_obj_t *d = lv_obj_create(head);
  lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(d, DOT_SZ, DOT_SZ);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(d, 0, 0);
  lv_obj_set_style_bg_color(d, dot, 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  if (primary) {
    lv_obj_set_style_shadow_color(d, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(d, 5, 0);
    lv_obj_set_style_shadow_opa(d, LV_OPA_COVER, 0);
  }

  lv_obj_t *spacer = bare_box(head, 1, 1);
  lv_obj_set_flex_grow(spacer, 1);

  lv_obj_t *key = lv_obj_create(head);
  lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(key, KEY_SZ, KEY_SZ);
  lv_obj_set_style_radius(key, KEY_RAD, 0);
  lv_obj_set_style_border_width(key, 0, 0);
  lv_obj_set_style_bg_color(key, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(key, primary ? LV_OPA_COVER : LV_OPA_40, 0);

  lv_obj_t *name = lv_label_create(tile);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, lv_pct(100));
  lv_label_set_text(name, s_slot[i].name);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);

  lv_obj_t *stub = lv_label_create(tile);
  lv_label_set_text(stub, s_slot[i].role);
  lv_obj_set_style_text_font(stub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(stub, lv_color_hex(COL_DIM), 0);

  lv_obj_t *bars = bare_box(tile, lv_pct(100), BAR_MAX_H);
  lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(bars, BAR_GAP, 0);
  for (int b = 0; b < BAR_CNT; b++) {
    int h = s_slot[i].bars[b] * BAR_MAX_H / 100;
    if (h < 3)
      h = 3;
    lv_obj_t *bar = lv_obj_create(bars);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, BAR_W, h);
    lv_obj_set_style_radius(bar, 1, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, sig, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  }
}

static void build_empty_tile(lv_obj_t *tile, int i) {
  (void)i;
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *key = lv_obj_create(tile);
  lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(key, KEY_SZ + 4, KEY_SZ + 4);
  lv_obj_set_style_radius(key, KEY_RAD, 0);
  lv_obj_set_style_border_width(key, 1, 0);
  lv_obj_set_style_border_color(key, lv_color_hex(COL_SLOT), 0);
  lv_obj_set_style_bg_opa(key, LV_OPA_TRANSP, 0);

  lv_obj_t *lbl = lv_label_create(tile);
  lv_label_set_text(lbl, "(empty)");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_SLOT), 0);
}

static lv_obj_t *make_tile(lv_obj_t *grid, int i) {
  bool filled = s_slot[i].used;

  lv_obj_t *tile = lv_obj_create(grid);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(tile, TILE_W, TILE_H);
  lv_obj_set_style_radius(tile, TILE_RAD, 0);
  lv_obj_set_style_pad_all(tile, TILE_PAD, 0);
  lv_obj_set_style_pad_row(tile, TILE_GAP, 0);
  lv_obj_set_style_border_width(tile, 1, 0);
  lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  if (filled) {
    lv_obj_set_style_bg_color(tile, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    build_filled_tile(tile, i);
  } else {
    lv_obj_set_style_bg_color(tile, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    build_empty_tile(tile, i);
  }
  return tile;
}

static void refresh_selection(void) {
  for (int i = 0; i < TILE_CNT; i++) {
    if (s_tiles[i] == NULL)
      continue;
    bool sel = (i == s_sel);
    lv_color_t base =
        s_slot[i].primary ? current_theme.border_accent : current_theme.border_inactive;
    lv_obj_set_style_border_color(s_tiles[i], sel ? current_theme.border_accent : base, 0);
    lv_obj_set_style_border_width(s_tiles[i], sel ? 2 : 1, 0);
    lv_obj_set_style_shadow_color(s_tiles[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(s_tiles[i], sel ? GLOW_W : 0, 0);
    lv_obj_set_style_shadow_opa(s_tiles[i], sel ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_spread(s_tiles[i], sel ? GLOW_SPR : 0, 0);
  }
}

static void build_grid_content(void) {
  for (int i = 0; i < TILE_CNT; i++)
    s_tiles[i] = NULL;
  s_empty_msg = NULL;
  if (s_grid != NULL)
    lv_obj_clean(s_grid);

  if (s_proto == LORA_PROTO_NONE) {
    s_empty_msg = lv_label_create(s_grid);
    lv_obj_add_flag(s_empty_msg, LV_OBJ_FLAG_FLOATING);
    lv_label_set_text(s_empty_msg, "Start a protocol first");
    lv_obj_set_style_text_font(s_empty_msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_empty_msg, lv_color_hex(COL_DIM), 0);
    lv_obj_center(s_empty_msg);
    return;
  }

  for (int i = 0; i < TILE_CNT; i++)
    s_tiles[i] = make_tile(s_grid, i);
  refresh_selection();
}

static void on_kb_rename(const char *text, void *user_data) {
  int idx = (int)(intptr_t)user_data;
  if (text == NULL || text[0] == '\0')
    return;
  if (idx < 0 || idx >= TILE_CNT)
    return;

  lora_proto_t proto = lora_session_active();
  if (proto == LORA_PROTO_MESHTASTIC) {
    const mt_channel_t *ch = mt_channel_get((uint8_t)idx);
    uint8_t psk[MT_PSK_SIZE];
    mt_channel_role_t role;
    if (ch != NULL && ch->is_used && ch->role != MT_CH_DISABLED) {
      memcpy(psk, ch->psk, sizeof(psk));
      role = ch->role;
    } else {
      esp_fill_random(psk, sizeof(psk));
      role = MT_CH_SECONDARY;
    }
    mt_channel_set((uint8_t)idx, text, psk, role);
  } else if (proto == LORA_PROTO_MESHCORE) {
    const meshcore_channel_t *ch = meshcore_channel_get((uint8_t)idx);
    uint8_t secret[KEY_LEN];
    if (ch != NULL && ch->is_used) {
      memcpy(secret, ch->secret, sizeof(secret));
    } else {
      esp_fill_random(secret, sizeof(secret));
    }
    meshcore_channel_set((uint8_t)idx, text, secret);
  } else {
    return;
  }

  ui_feedback(UI_FB_WRITE);
  notify(NOTIFY_SAVED, "Channel saved");
  load_slots();
  build_grid_content();
}

static void disable_slot(int idx) {
  if (idx < 0 || idx >= TILE_CNT)
    return;
  if (!s_slot[idx].used) {
    ui_feedback(UI_FB_SELECT);
    return;
  }
  if (s_proto == LORA_PROTO_MESHTASTIC)
    mt_channel_disable((uint8_t)idx);
  else if (s_proto == LORA_PROTO_MESHCORE)
    meshcore_channel_set((uint8_t)idx, NULL, NULL);
  else
    return;

  ui_feedback(UI_FB_WRITE);
  notify(NOTIFY_SAVED, "Channel disabled");
  load_slots();
  build_grid_content();
}

static void lora_channels_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  const bool active = (s_proto != LORA_PROTO_NONE);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_LORA_CHAT);
      break;
    case INPUT_BTN_DOWN:
      if (nav && active) {
        s_sel = (s_sel + 1) % TILE_CNT;
        refresh_selection();
        if (s_tiles[s_sel] != NULL)
          lv_obj_scroll_to_view(s_tiles[s_sel], LV_ANIM_ON);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && active) {
        s_sel = (s_sel - 1 + TILE_CNT) % TILE_CNT;
        refresh_selection();
        if (s_tiles[s_sel] != NULL)
          lv_obj_scroll_to_view(s_tiles[s_sel], LV_ANIM_ON);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        if (active) {
          ui_feedback(UI_FB_SELECT);
          keyboard_open(NULL, on_kb_rename, (void *)(intptr_t)s_sel);
        } else {
          ui_feedback(UI_FB_SELECT);
        }
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press && active)
        disable_slot(s_sel);
      break;
    default:
      break;
  }
}

void ui_lora_channels_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_grid = NULL;
  s_empty_msg = NULL;
  for (int i = 0; i < TILE_CNT; i++)
    s_tiles[i] = NULL;
  s_sel = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  lv_obj_t *grid = lv_obj_create(s_screen);
  lv_obj_set_size(grid, LCD_H_RES, BODY_H);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, BODY_TOP);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, GRID_PAD, 0);
  lv_obj_set_style_pad_row(grid, GRID_GAP, 0);
  lv_obj_set_style_pad_column(grid, GRID_GAP, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_bg_color(grid, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(grid, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(grid, 2, LV_PART_SCROLLBAR);
  s_grid = grid;

  load_slots();
  build_grid_content();

  ui_chrome_footer(s_screen, FOOTER_HINT);

  ui_input_set_screen_handler(lora_channels_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
