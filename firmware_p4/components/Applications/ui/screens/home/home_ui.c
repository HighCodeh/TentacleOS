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

#include "home_ui.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "st7789.h"

#include "assets_manager.h"
#include "core/lv_group.h"
#include "dropdown_ui.h"
#include "favorites.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "menu_ui.h"
#include "sys_time.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "HOME_UI";

#define HOME_HEADER_HEIGHT_PCT 9
#define HOME_HEADER_HEIGHT     ((LCD_V_RES * HOME_HEADER_HEIGHT_PCT) / 100)

#define HOME_ART_ASSET "/assets/img/image.bin"
#define HOME_DATE_FONT "A:assets/fonts/Inter.bin"

#define HOME_PAD       10
#define HOME_ROW_GAP   8
#define HOME_INNER_W   (LCD_H_RES - 2 * HOME_PAD)
#define HOME_ART_MAX_W 210
#define HOME_ART_MAX_H 132
#define HOME_FLOAT_AMP 5
#define HOME_FLOAT_MS  1600

#define HOME_FAV_MAX      5
#define HOME_USER_FAV_MAX (HOME_FAV_MAX - 1)
#define HOME_OCTO_NAME    "OCTOBIT"
#define HOME_TAG_LEN      4
#define HOME_FAV_GAP      8
#define HOME_TILE_MAX     50
#define HOME_TILE_MIN     30
#define HOME_TILE_RADIUS  12

#define COL_INK   0xD3DBD8
#define COL_LINE  0x223029
#define COL_DIM   0x6D7A75
#define COL_GREEN 0x00E676

static lv_obj_t *s_screen_home = NULL;
static lv_obj_t *s_fav_tiles[HOME_FAV_MAX];
static screen_id_t s_fav_targets[HOME_FAV_MAX];
static const char *s_fav_names[HOME_FAV_MAX];
static int s_fav_count = 0;
static int s_fav_focus = 0;
static lv_font_t *s_date_font = NULL;

static void home_event_cb(lv_event_t *e);

static void str_upper(char *s) {
  for (; *s != '\0'; s++)
    if (*s >= 'a' && *s <= 'z')
      *s = (char)(*s - 'a' + 'A');
}

static void fav_tag(const char *name, char *out, size_t outsz) {
  size_t j = 0;
  for (size_t i = 0; name[i] != '\0' && j + 1 < outsz && j < HOME_TAG_LEN; i++) {
    char ch = name[i];
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
      out[j++] = ch;
    else if (ch >= 'a' && ch <= 'z')
      out[j++] = (char)(ch - 'a' + 'A');
  }
  out[j] = '\0';
  if (j == 0 && outsz > 1) {
    out[0] = '?';
    out[1] = '\0';
  }
}

static const char *icon_for_screen(screen_id_t s) {
  switch (s) {
    case SCREEN_WIFI_MENU:
      return "/assets/icons/wifi.bin";
    case SCREEN_BLE_MENU:
      return "/assets/icons/bluetooth.bin";
    case SCREEN_NFC_MENU:
      return "/assets/icons/nfc.bin";
    case SCREEN_RFID_MENU:
      return "/assets/icons/contactless.bin";
    case SCREEN_IR_MENU:
      return "/assets/icons/settings_input_antenna.bin";
    case SCREEN_SUBGHZ_MENU:
      return "/assets/icons/cell_tower.bin";
    case SCREEN_LORA_CHAT:
      return "/assets/icons/router.bin";
    case SCREEN_BADUSB_MENU:
      return "/assets/icons/usb.bin";
    case SCREEN_GPIO:
      return "/assets/icons/developer_board.bin";
    case SCREEN_SETTINGS:
      return "/assets/icons/settings.bin";
    case SCREEN_FILES:
      return "/assets/icons/folder.bin";
    case SCREEN_PLAYER:
      return "/assets/icons/music_note.bin";
    case SCREEN_DEV_MENU:
      return "/assets/icons/developer_board.bin";
    case SCREEN_OCTOBIT_STATUS:
      return "/assets/icons/monitoring.bin";
    default:
      return NULL;
  }
}

static lv_obj_t *make_group(lv_obj_t *parent, lv_flex_flow_t flow, int gap) {
  lv_obj_t *g = lv_obj_create(parent);
  lv_obj_set_width(g, LV_PCT(100));
  lv_obj_set_height(g, LV_SIZE_CONTENT);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g, 0, 0);
  lv_obj_set_style_pad_all(g, 0, 0);
  lv_obj_set_flex_flow(g, flow);
  lv_obj_set_flex_align(g, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (flow == LV_FLEX_FLOW_ROW)
    lv_obj_set_style_pad_column(g, gap, 0);
  else
    lv_obj_set_style_pad_row(g, gap, 0);
  return g;
}

static void make_fav_tile(lv_obj_t *row, int idx, int tile_sz) {
  lv_obj_t *t = lv_obj_create(row);
  lv_obj_set_size(t, tile_sz, tile_sz);
  lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(t, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_10, 0);
  lv_obj_set_style_border_color(t, lv_color_hex(COL_LINE), 0);
  lv_obj_set_style_border_width(t, 1, 0);
  lv_obj_set_style_radius(t, HOME_TILE_RADIUS, 0);
  lv_obj_set_style_pad_all(t, 0, 0);

  const char *icon_path = icon_for_screen(s_fav_targets[idx]);
  lv_image_dsc_t *icon = icon_path != NULL ? assets_get(icon_path) : NULL;
  if (icon != NULL) {
    lv_obj_t *img = lv_image_create(t);
    lv_image_set_src(img, icon);
    lv_image_set_antialias(img, false);
    int w = icon->header.w;
    int target = tile_sz - 16;
    if (w > target && w > 0) {
      lv_image_set_pivot(img, w / 2, icon->header.h / 2);
      lv_image_set_scale(img, (uint16_t)(target * 256 / w));
    }
    lv_obj_set_style_image_recolor(img, lv_color_hex(COL_INK), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  } else {
    char tag[HOME_TAG_LEN + 1];
    fav_tag(s_fav_names[idx], tag, sizeof(tag));
    lv_obj_t *l = lv_label_create(t);
    lv_label_set_text(l, tag);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_INK), 0);
    lv_obj_center(l);
  }
  s_fav_tiles[idx] = t;
}

static void refresh_fav_focus(void) {
  for (int i = 0; i < s_fav_count; i++) {
    bool sel = (i == s_fav_focus);
    lv_obj_set_style_border_color(s_fav_tiles[i], lv_color_hex(sel ? COL_GREEN : COL_LINE), 0);
    lv_obj_set_style_border_width(s_fav_tiles[i], sel ? 2 : 1, 0);
    lv_obj_set_style_shadow_color(s_fav_tiles[i], lv_color_hex(COL_GREEN), 0);
    lv_obj_set_style_shadow_width(s_fav_tiles[i], sel ? 16 : 0, 0);
    lv_obj_set_style_shadow_opa(s_fav_tiles[i], sel ? LV_OPA_40 : LV_OPA_TRANSP, 0);
  }
}

static void build_date(lv_obj_t *content) {
  lv_obj_t *grp = make_group(content, LV_FLEX_FLOW_COLUMN, 2);

  char big[16];
  if (!sys_time_format(big, sizeof(big), "%d %b"))
    big[0] = '\0';
  str_upper(big);

  lv_obj_t *date = lv_label_create(grp);
  lv_label_set_text(date, big);
  lv_obj_set_style_text_font(date, s_date_font != NULL ? s_date_font : &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(date, lv_color_hex(COL_GREEN), 0);

  char sub[24];
  if (!sys_time_format(sub, sizeof(sub), "%A  %Y"))
    sub[0] = '\0';
  str_upper(sub);

  lv_obj_t *wk = lv_label_create(grp);
  lv_label_set_text(wk, sub);
  lv_obj_set_style_text_font(wk, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(wk, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_letter_space(wk, 2, 0);
}

static void float_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void build_octobit(lv_obj_t *content) {
  lv_obj_t *stage = lv_obj_create(content);
  lv_obj_set_width(stage, LV_PCT(100));
  lv_obj_set_height(stage, 0);
  lv_obj_set_flex_grow(stage, 1);
  lv_obj_remove_flag(stage, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(stage, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(stage, 0, 0);
  lv_obj_set_style_pad_all(stage, 0, 0);

  static lv_image_dsc_t *s_art_dsc = NULL;
  if (s_art_dsc == NULL)
    s_art_dsc = assets_get(HOME_ART_ASSET);
  if (s_art_dsc == NULL)
    return;

  lv_obj_t *art = lv_image_create(stage);
  lv_image_set_src(art, s_art_dsc);
  lv_image_set_antialias(art, false);

  int w = s_art_dsc->header.w;
  int h = s_art_dsc->header.h;
  int sx = (w > 0) ? (HOME_ART_MAX_W * 256 / w) : 256;
  int sy = (h > 0) ? (HOME_ART_MAX_H * 256 / h) : 256;
  int scale = (sx < sy) ? sx : sy;
  if (scale > 256)
    scale = 256;
  if (scale != 256) {
    lv_image_set_pivot(art, w / 2, h / 2);
    lv_image_set_scale(art, (uint16_t)scale);
  }
  lv_obj_align(art, LV_ALIGN_CENTER, 0, 0);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, art);
  lv_anim_set_values(&a, -HOME_FLOAT_AMP, HOME_FLOAT_AMP);
  lv_anim_set_duration(&a, HOME_FLOAT_MS);
  lv_anim_set_playback_duration(&a, HOME_FLOAT_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, float_anim_cb);
  lv_anim_start(&a);
}

static void build_favorites(lv_obj_t *content) {
  s_fav_count = 0;
  s_fav_focus = 0;

  int n = menu_catalog_count();
  for (int i = 0; i < n && s_fav_count < HOME_USER_FAV_MAX; i++) {
    screen_id_t tgt = menu_catalog_target(i);
    if (favorites_is(tgt)) {
      s_fav_targets[s_fav_count] = tgt;
      s_fav_names[s_fav_count] = menu_catalog_name(i);
      s_fav_count++;
    }
  }

  if (s_fav_count == 0) {
    s_fav_targets[s_fav_count] = SCREEN_SETTINGS;
    s_fav_names[s_fav_count] = "CONFIG";
    s_fav_count++;
  }

  s_fav_targets[s_fav_count] = SCREEN_OCTOBIT_STATUS;
  s_fav_names[s_fav_count] = HOME_OCTO_NAME;
  s_fav_count++;

  int tile = (HOME_INNER_W - (s_fav_count - 1) * HOME_FAV_GAP) / s_fav_count;
  if (tile > HOME_TILE_MAX)
    tile = HOME_TILE_MAX;
  if (tile < HOME_TILE_MIN)
    tile = HOME_TILE_MIN;

  lv_obj_t *favs = make_group(content, LV_FLEX_FLOW_ROW, HOME_FAV_GAP);
  for (int i = 0; i < s_fav_count; i++)
    make_fav_tile(favs, i, tile);
  refresh_fav_focus();
}

void ui_home_open(void) {
  ESP_LOGI(TAG,
           "home open: free=%u largest=%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  if (s_screen_home != NULL) {
    lv_obj_del(s_screen_home);
    s_screen_home = NULL;
  }

  if (s_date_font == NULL)
    s_date_font = lv_binfont_create(HOME_DATE_FONT);

  s_screen_home = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen_home, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen_home, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen_home, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen_home);

  lv_obj_t *content = lv_obj_create(s_screen_home);
  lv_obj_set_size(content, LCD_H_RES, LCD_V_RES - HOME_HEADER_HEIGHT);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, HOME_HEADER_HEIGHT);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_left(content, HOME_PAD, 0);
  lv_obj_set_style_pad_right(content, HOME_PAD, 0);
  lv_obj_set_style_pad_top(content, 8, 0);
  lv_obj_set_style_pad_bottom(content, HOME_PAD, 0);
  lv_obj_set_style_pad_row(content, HOME_ROW_GAP, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  build_date(content);
  build_octobit(content);
  build_favorites(content);

  lv_obj_add_event_cb(s_screen_home, home_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_screen_home);
    lv_group_focus_obj(s_screen_home);
  }

  ui_screen_load_owned(&s_screen_home, s_screen_home);
}

static void home_event_cb(lv_event_t *e) {
  if (dropdown_ui_is_open())
    return;

  if (ui_input_is_locked())
    return;

  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;

  uint32_t key = lv_event_get_key(e);

  if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
    if (key == LV_KEY_RIGHT)
      s_fav_focus = (s_fav_focus + 1) % s_fav_count;
    else
      s_fav_focus = (s_fav_focus == 0) ? s_fav_count - 1 : s_fav_focus - 1;
    ui_feedback(UI_FB_NAV);
    refresh_fav_focus();
    return;
  }

  if (key == LV_KEY_ENTER) {
    ui_switch_screen(s_fav_targets[s_fav_focus]);
    return;
  }

  if (key == LV_KEY_DOWN) {
    ui_switch_screen(SCREEN_MENU);
    return;
  }
}
