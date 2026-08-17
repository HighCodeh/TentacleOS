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

#include "lora_securedm_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "lvgl.h"
#include "st7789.h"

#include "keyboard_ui.h"
#include "lora_session.h"
#include "meshcore.h"
#include "meshtastic_mesh.h"
#include "meshtastic_nodedb.h"
#include "meshtastic_pki.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define HDR_TITLE   "SECURE DM"
#define HDR_ICON    NULL
#define FOOTER_LIST "UP/DOWN   OK OPEN   BACK"
#define FOOTER_CHAT "OK WRITE   UP/DOWN SCROLL   BACK"

#define BODY_TOP UI_CHROME_HEADER_H
#define BODY_BOT (LCD_V_RES - UI_CHROME_FOOTER_H)

#define BANNER_H 36
#define INPUT_H  32

#define BANNER_PAD_H 8
#define BANNER_GAP   6

#define LIST_PAD 8
#define LIST_GAP 6

#define BUBBLE_MAX_W 186
#define BUBBLE_PAD   7
#define BUBBLE_RAD   9

#define LOCK_W    12
#define LOCK_H    14
#define LOCK_SH_W 8
#define LOCK_SH_H 6
#define LOCK_BD_W 12
#define LOCK_BD_H 8
#define LOCK_LINE 2

#define SEAL_SZ 7

#define INPUT_PAD_H 8
#define INPUT_GAP   6
#define PILL_RAD    12
#define PILL_PAD_H  9

#define SCROLL_STEP 36

#define ROW_GAP    8
#define ROW_GLOW_W 14
#define POLL_MS    1000

#define DM_MAX_CONTACTS 32

#define COL_DIM   0x8A8594
#define COL_OK    0x00E676
#define COL_OUTTX 0xF2EEFF
#define COL_OUTGR 0x2A1F52

typedef enum {
  DM_VIEW_CONTACTS = 0,
  DM_VIEW_THREAD,
} dm_view_t;

typedef struct {
  char name[MESHCORE_NAME_MAX];
  char fp[16];
  uint8_t pub_key[32];
  uint32_t num;
  bool has_key;
} dm_contact_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_rows[DM_MAX_CONTACTS];
static lv_obj_t *s_row_name[DM_MAX_CONTACTS];
static lv_timer_t *s_poll = NULL;

static dm_view_t s_view = DM_VIEW_CONTACTS;
static lora_proto_t s_proto = LORA_PROTO_NONE;
static dm_contact_t s_contacts[DM_MAX_CONTACTS];
static int s_contact_count = 0;
static int s_sel = 0;
static dm_contact_t s_peer;
static bool s_have_peer = false;

static void lora_securedm_input(const input_event_t *ev, void *ctx);
static void build_screen(void);

static void stop_timers(void) {
  if (s_poll != NULL) {
    lv_timer_delete(s_poll);
    s_poll = NULL;
  }
}

static void fp_from_key(const uint8_t *key, char *out, size_t n) {
  if (key == NULL) {
    snprintf(out, n, "----");
    return;
  }
  snprintf(out, n, "%02X%02X %02X%02X", key[0], key[1], key[2], key[3]);
}

static void load_contacts(void) {
  s_contact_count = 0;
  s_proto = lora_session_active();

  if (s_proto == LORA_PROTO_MESHCORE) {
    const meshcore_contact_t *arr = meshcore_contacts_array();
    if (arr != NULL) {
      for (size_t i = 0; i < MESHCORE_MAX_CONTACTS && s_contact_count < DM_MAX_CONTACTS; i++) {
        if (!arr[i].is_used)
          continue;
        dm_contact_t *c = &s_contacts[s_contact_count];
        snprintf(c->name, sizeof(c->name), "%s", arr[i].name[0] != '\0' ? arr[i].name : "contact");
        memcpy(c->pub_key, arr[i].pub_key, sizeof(c->pub_key));
        c->num = 0;
        c->has_key = true;
        fp_from_key(arr[i].pub_key, c->fp, sizeof(c->fp));
        s_contact_count++;
      }
    }
  } else if (s_proto == LORA_PROTO_MESHTASTIC) {
    uint16_t total = mt_nodedb_count();
    for (uint16_t i = 0; i < total && s_contact_count < DM_MAX_CONTACTS; i++) {
      const mt_node_entry_t *n = mt_nodedb_get_by_index(i);
      if (n == NULL || !n->in_use || !n->has_public_key)
        continue;
      dm_contact_t *c = &s_contacts[s_contact_count];
      const char *nm = (n->long_name[0] != '\0')
                           ? n->long_name
                           : ((n->short_name[0] != '\0') ? n->short_name : "node");
      snprintf(c->name, sizeof(c->name), "%s", nm);
      memset(c->pub_key, 0, sizeof(c->pub_key));
      c->num = n->num;
      c->has_key = true;
      fp_from_key(n->public_key, c->fp, sizeof(c->fp));
      s_contact_count++;
    }
  }

  if (s_sel >= s_contact_count)
    s_sel = (s_contact_count > 0) ? s_contact_count - 1 : 0;
}

static void own_identity(char *name, size_t nmax, char *sub, size_t smax) {
  if (s_proto == LORA_PROTO_MESHCORE) {
    meshcore_get_name(name, nmax);
    if (name[0] == '\0')
      snprintf(name, nmax, "You");
    uint8_t k[32];
    meshcore_get_pub_key(k);
    fp_from_key(k, sub, smax);
  } else if (s_proto == LORA_PROTO_MESHTASTIC) {
    snprintf(name, nmax, "You");
    fp_from_key(mt_pki_get_pubkey(), sub, smax);
  } else {
    snprintf(name, nmax, "No protocol");
    snprintf(sub, smax, "offline");
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

static lv_obj_t *make_lock(lv_obj_t *parent, lv_color_t col) {
  lv_obj_t *box = bare_box(parent, LOCK_W, LOCK_H);

  lv_obj_t *sh = lv_obj_create(box);
  lv_obj_remove_flag(sh, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(sh, LOCK_SH_W, LOCK_SH_H);
  lv_obj_align(sh, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(sh, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(sh, 0, 0);
  lv_obj_set_style_radius(sh, 4, 0);
  lv_obj_set_style_border_width(sh, LOCK_LINE, 0);
  lv_obj_set_style_border_color(sh, col, 0);
  lv_obj_set_style_border_side(
      sh, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);

  lv_obj_t *bd = lv_obj_create(box);
  lv_obj_remove_flag(bd, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(bd, LOCK_BD_W, LOCK_BD_H);
  lv_obj_align(bd, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(bd, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(bd, 0, 0);
  lv_obj_set_style_radius(bd, 2, 0);
  lv_obj_set_style_border_width(bd, LOCK_LINE, 0);
  lv_obj_set_style_border_color(bd, col, 0);
  return box;
}

static lv_obj_t *make_seal(lv_obj_t *parent, lv_color_t col) {
  lv_obj_t *s = lv_obj_create(parent);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s, SEAL_SZ, SEAL_SZ);
  lv_obj_set_style_radius(s, 2, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  lv_obj_set_style_bg_color(s, col, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  return s;
}

static void build_banner(lv_obj_t *parent, const char *name, const char *sub, bool secure) {
  lv_color_t acc = secure ? lv_color_hex(COL_OK) : lv_color_hex(COL_DIM);

  lv_obj_t *banner = lv_obj_create(parent);
  lv_obj_remove_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(banner, LCD_H_RES, BANNER_H);
  lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, BODY_TOP);
  lv_obj_set_style_radius(banner, 0, 0);
  lv_obj_set_style_bg_color(banner, acc, 0);
  lv_obj_set_style_bg_opa(banner, 30, 0);
  lv_obj_set_style_border_width(banner, 1, 0);
  lv_obj_set_style_border_color(banner, acc, 0);
  lv_obj_set_style_border_side(banner, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_opa(banner, LV_OPA_50, 0);
  lv_obj_set_style_pad_hor(banner, BANNER_PAD_H, 0);
  lv_obj_set_style_pad_ver(banner, 0, 0);
  lv_obj_set_style_pad_column(banner, BANNER_GAP, 0);
  lv_obj_set_flex_flow(banner, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(banner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  make_lock(banner, acc);

  lv_obj_t *col = bare_box(banner, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(col, 1, 0);
  lv_obj_set_flex_grow(col, 1);

  lv_obj_t *nm = lv_label_create(col);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_set_width(nm, lv_pct(100));
  lv_label_set_text(nm, name);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);

  lv_obj_t *fp = lv_label_create(col);
  lv_label_set_text(fp, sub);
  lv_obj_set_style_text_font(fp, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(fp, acc, 0);

  lv_obj_t *e2ee = lv_label_create(banner);
  lv_label_set_text(e2ee, "E2EE");
  lv_obj_set_style_text_font(e2ee, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(e2ee, acc, 0);
}

static void add_bubble(lv_obj_t *list, bool outgoing, const char *text, const char *ts) {
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row,
                        outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *bubble = lv_obj_create(row);
  lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(bubble, BUBBLE_MAX_W, 0);
  lv_obj_set_style_pad_all(bubble, BUBBLE_PAD, 0);
  lv_obj_set_style_radius(bubble, BUBBLE_RAD, 0);
  lv_obj_set_style_pad_row(bubble, 3, 0);
  lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_border_width(bubble, 1, 0);
  if (outgoing) {
    lv_obj_set_style_bg_color(bubble, current_theme.border_accent, 0);
    lv_obj_set_style_bg_grad_color(bubble, lv_color_hex(COL_OUTGR), 0);
    lv_obj_set_style_bg_grad_dir(bubble, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bubble, current_theme.border_accent, 0);
  } else {
    lv_obj_set_style_bg_color(bubble, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_dir(bubble, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bubble, current_theme.border_inactive, 0);
  }

  lv_obj_t *body = lv_label_create(bubble);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_max_width(body, BUBBLE_MAX_W - 2 * BUBBLE_PAD, 0);
  lv_label_set_text(body, text);
  lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(
      body, outgoing ? lv_color_hex(COL_OUTTX) : current_theme.text_main, 0);

  lv_obj_t *meta = bare_box(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(meta, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(meta, 4, 0);
  if (outgoing) {
    lv_obj_set_flex_align(meta, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  } else {
    make_seal(meta, lv_color_hex(COL_OK));
  }

  lv_obj_t *tlbl = lv_label_create(meta);
  if (outgoing) {
    char buf[24];
    lv_snprintf(buf, sizeof(buf), "%s %s%s", ts, LV_SYMBOL_OK, LV_SYMBOL_OK);
    lv_label_set_text(tlbl, buf);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(COL_OUTTX), 0);
    lv_obj_set_style_text_opa(tlbl, LV_OPA_70, 0);
  } else {
    lv_label_set_text(tlbl, ts);
    lv_obj_set_style_text_color(tlbl, lv_color_hex(COL_DIM), 0);
  }
  lv_obj_set_style_text_font(tlbl, &lv_font_montserrat_12, 0);

  lv_obj_scroll_to_view(row, LV_ANIM_ON);
}

static void build_list_container(lv_obj_t *parent) {
  s_list = lv_obj_create(parent);
  lv_obj_set_size(s_list, LCD_H_RES, BODY_BOT - INPUT_H - (BODY_TOP + BANNER_H));
  lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, BODY_TOP + BANNER_H);
  lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_list, 0, 0);
  lv_obj_set_style_pad_all(s_list, LIST_PAD, 0);
  lv_obj_set_style_pad_row(s_list, LIST_GAP, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_bg_color(s_list, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(s_list, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(s_list, 2, LV_PART_SCROLLBAR);
}

static void add_placeholder(lv_obj_t *list, const char *txt) {
  lv_obj_t *l = lv_label_create(list);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
}

static void make_contact_row(lv_obj_t *list, int i, const dm_contact_t *c) {
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(row, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_radius(row, BUBBLE_RAD, 0);
  lv_obj_set_style_pad_all(row, BUBBLE_PAD, 0);
  lv_obj_set_style_pad_column(row, ROW_GAP, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, current_theme.border_inactive, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  make_lock(row, lv_color_hex(COL_OK));

  lv_obj_t *col = bare_box(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(col, 1, 0);
  lv_obj_set_flex_grow(col, 1);

  lv_obj_t *nm = lv_label_create(col);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_set_width(nm, lv_pct(100));
  lv_label_set_text(nm, c->name);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(nm, lv_color_hex(COL_DIM), 0);

  lv_obj_t *fp = lv_label_create(col);
  char sub[24];
  lv_snprintf(sub, sizeof(sub), "key %s", c->fp);
  lv_label_set_text(fp, sub);
  lv_obj_set_style_text_font(fp, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(fp, lv_color_hex(COL_OK), 0);

  s_rows[i] = row;
  s_row_name[i] = nm;
}

static void row_style(int i, bool selected) {
  lv_obj_t *row = s_rows[i];
  if (row == NULL)
    return;
  lv_obj_set_style_bg_color(
      row, selected ? current_theme.bg_secondary : current_theme.bg_primary, 0);
  lv_obj_set_style_border_color(
      row, selected ? current_theme.border_accent : current_theme.border_inactive, 0);
  lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
  if (selected) {
    lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(row, ROW_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(row, -2, 0);
  } else {
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
  }
  if (s_row_name[i] != NULL)
    lv_obj_set_style_text_color(
        s_row_name[i], selected ? current_theme.text_main : lv_color_hex(COL_DIM), 0);
}

static void select_contact(int sel) {
  if (s_contact_count <= 0)
    return;
  if (sel < 0)
    sel = 0;
  if (sel >= s_contact_count)
    sel = s_contact_count - 1;
  s_sel = sel;
  for (int i = 0; i < s_contact_count; i++)
    row_style(i, i == sel);
  if (s_list != NULL && s_rows[sel] != NULL) {
    lv_obj_update_layout(s_list);
    lv_obj_scroll_to_view(s_rows[sel], LV_ANIM_ON);
  }
}

static void build_contacts_list(lv_obj_t *parent) {
  build_list_container(parent);
  if (s_proto == LORA_PROTO_NONE) {
    add_placeholder(s_list, "Start a protocol first");
    return;
  }
  if (s_contact_count == 0) {
    add_placeholder(s_list, "No contacts yet");
    return;
  }
  for (int i = 0; i < s_contact_count; i++)
    make_contact_row(s_list, i, &s_contacts[i]);
  select_contact(s_sel);
}

static void build_thread_list(lv_obj_t *parent) {
  build_list_container(parent);
  add_placeholder(s_list, "End-to-end encrypted. OK to write.");
}

static void build_input(lv_obj_t *parent, const char *placeholder) {
  lv_obj_t *strip = lv_obj_create(parent);
  lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(strip, LCD_H_RES, INPUT_H);
  lv_obj_align(strip, LV_ALIGN_TOP_MID, 0, BODY_BOT - INPUT_H);
  lv_obj_set_style_radius(strip, 0, 0);
  lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(strip, 1, 0);
  lv_obj_set_style_border_color(strip, current_theme.border_inactive, 0);
  lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_hor(strip, INPUT_PAD_H, 0);
  lv_obj_set_style_pad_ver(strip, 0, 0);
  lv_obj_set_style_pad_column(strip, INPUT_GAP, 0);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  make_lock(strip, lv_color_hex(COL_OK));

  lv_obj_t *pill = lv_obj_create(strip);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(pill, 22);
  lv_obj_set_flex_grow(pill, 1);
  lv_obj_set_style_radius(pill, PILL_RAD, 0);
  lv_obj_set_style_bg_color(pill, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(pill, 1, 0);
  lv_obj_set_style_border_color(pill, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_hor(pill, PILL_PAD_H, 0);
  lv_obj_set_style_pad_ver(pill, 0, 0);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ph = lv_label_create(pill);
  lv_label_set_text(ph, placeholder);
  lv_obj_set_style_text_font(ph, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(ph, lv_color_hex(COL_DIM), 0);
}

static void on_kb_submit(const char *text, void *user_data) {
  (void)user_data;
  if (text == NULL || text[0] == '\0')
    return;
  if (s_view != DM_VIEW_THREAD || s_list == NULL || !s_have_peer)
    return;

  esp_err_t err = ESP_ERR_INVALID_STATE;
  if (s_proto == LORA_PROTO_MESHCORE) {
    uint32_t ack = 0;
    err = meshcore_send_direct_msg(s_peer.pub_key, text, 0, 0, &ack);
  } else if (s_proto == LORA_PROTO_MESHTASTIC) {
    err = meshtastic_mesh_send_text(text, s_peer.num);
  }

  if (err == ESP_OK) {
    ui_feedback(UI_FB_WRITE);
    add_bubble(s_list, true, text, "sent");
    notify(NOTIFY_LORA, "Encrypted DM sent");
  } else {
    notify(NOTIFY_LORA, "Send failed");
  }
}

static void contacts_poll_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen || s_view != DM_VIEW_CONTACTS) {
    lv_timer_delete(t);
    s_poll = NULL;
    return;
  }
  int prev_count = s_contact_count;
  lora_proto_t prev_proto = s_proto;
  load_contacts();
  if (s_contact_count != prev_count || s_proto != prev_proto)
    build_screen();
}

static void build_screen(void) {
  stop_timers();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_list = NULL;
  for (int i = 0; i < DM_MAX_CONTACTS; i++) {
    s_rows[i] = NULL;
    s_row_name[i] = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  if (s_view == DM_VIEW_CONTACTS) {
    load_contacts();
    char name[MESHCORE_NAME_MAX];
    char sub[24];
    own_identity(name, sizeof(name), sub, sizeof(sub));
    build_banner(s_screen, name, sub, s_proto != LORA_PROTO_NONE);
    build_contacts_list(s_screen);
    build_input(s_screen, "select a contact...");
    ui_chrome_footer(s_screen, FOOTER_LIST);
    s_poll = lv_timer_create(contacts_poll_cb, POLL_MS, NULL);
  } else {
    char sub[24];
    lv_snprintf(sub, sizeof(sub), "key %s", s_peer.fp);
    build_banner(s_screen, s_peer.name, sub, s_peer.has_key);
    build_thread_list(s_screen);
    build_input(s_screen, "sealed message...");
    ui_chrome_footer(s_screen, FOOTER_CHAT);
  }

  ui_input_set_screen_handler(lora_securedm_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void lora_securedm_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_view == DM_VIEW_CONTACTS) {
    switch (ev->button) {
      case INPUT_BTN_BACK:
      case INPUT_BTN_LEFT:
        if (press)
          ui_switch_screen(SCREEN_LORA_CHAT);
        break;
      case INPUT_BTN_DOWN:
        if (nav && s_contact_count > 0)
          select_contact((s_sel + 1) % s_contact_count);
        break;
      case INPUT_BTN_UP:
        if (nav && s_contact_count > 0)
          select_contact((s_sel - 1 + s_contact_count) % s_contact_count);
        break;
      case INPUT_BTN_OK:
        if (press && s_contact_count > 0 && s_sel >= 0 && s_sel < s_contact_count) {
          s_peer = s_contacts[s_sel];
          s_have_peer = true;
          ui_feedback(UI_FB_SELECT);
          s_view = DM_VIEW_THREAD;
          build_screen();
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
      if (press) {
        s_view = DM_VIEW_CONTACTS;
        build_screen();
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav && s_list != NULL) {
        int32_t sb = lv_obj_get_scroll_bottom(s_list);
        if (sb > 0)
          lv_obj_scroll_by(s_list, 0, -(sb < SCROLL_STEP ? sb : SCROLL_STEP), LV_ANIM_ON);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && s_list != NULL) {
        int32_t st = lv_obj_get_scroll_top(s_list);
        if (st > 0)
          lv_obj_scroll_by(s_list, 0, (st < SCROLL_STEP ? st : SCROLL_STEP), LV_ANIM_ON);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        ui_feedback(UI_FB_SELECT);
        keyboard_open(NULL, on_kb_submit, NULL);
      }
      break;
    default:
      break;
  }
}

void ui_lora_securedm_open(void) {
  s_view = DM_VIEW_CONTACTS;
  s_sel = 0;
  s_contact_count = 0;
  s_have_peer = false;
  build_screen();
}
