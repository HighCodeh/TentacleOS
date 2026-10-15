// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// Selected SD artwork and readable app identity. Signed bundles still use the
// registry verification and capability-consent path, never a UI-side bypass.
#include "games_menu_ui.h"
#include "esp_attr.h"
#include "muse_menu_icon.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "esp_attr.h"
#include "app_registry.h"
#include "assets_manager.h"
#include "game_cover.h"
#include "storage_init.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"

#define PURPLE 0xBD82FF
#define MUTED  0x9993AA
typedef struct {
  const char *name, *icon, *cover, *detail;
  int target;
} game_t;
static const game_t GAMES[] = {
    {"Game Boy", "/assets/icons/game_gb.bin", "gameboy", "SD game library", SCREEN_GAME_GB},
    {"DOOM", "/assets/icons/sports_esports.bin", "doom", "Requires SD WAD", SCREEN_GAME_DOOM},
    {"Snake", "/assets/icons/game_snake.bin", NULL, "Quick arcade", SCREEN_GAME_SNAKE},
    {"Breakout", "/assets/icons/game_brk.bin", NULL, "Brick breaker", SCREEN_GAME_BREAKOUT},
    {"Mosca", "/assets/icons/sports_esports.bin", NULL, "3D fly", SCREEN_FLY},
    {"Muse", "/assets/icons/animation.bin", NULL, "Voice notes", SCREEN_MUSE},
    {"Grok Bot", "/assets/icons/animation.bin", NULL, "Offline animation", SCREEN_GROK_IDLE},
};
#define GAME_COUNT ((int)(sizeof(GAMES) / sizeof(GAMES[0])))
static lv_obj_t *s_screen, *s_cover, *s_glyph, *s_wordmark, *s_name, *s_detail, *s_count_label,
    *s_kind;
EXT_RAM_BSS_ATTR static app_reg_entry_t s_reg[APP_REG_MAX];
static int s_reg_count, s_sel, s_art_w, s_art_h;

static void release_registry(lv_event_t *event) {
  (void)event;
  // Screen DELETE precedes recursive child deletion. Detach the one widget
  // which may reference registry pixels before freeing those pixels.
  if (s_glyph != NULL)
    lv_image_set_src(s_glyph, NULL);
  s_glyph = NULL;
  for (int i = 0; i < s_reg_count; ++i)
    lv_image_cache_drop(&s_reg[i].icon);
  app_registry_free(s_reg, s_reg_count);
  s_reg_count = 0;
}

static lv_obj_t *text_at(lv_obj_t *parent, int x, int y, int w, uint32_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, w);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  return label;
}

static void fade_icon(void *obj, int32_t value) {
  lv_obj_set_style_opa(obj, (lv_opa_t)value, 0);
}
static void update_view(void) {
  lv_label_set_text_fmt(s_count_label, "%02d / %02d", s_sel + 1, GAME_COUNT + s_reg_count);
  const char *name, *detail, *kind, *icon = NULL;
  const lv_image_dsc_t *embedded = NULL;
  bool cover_loaded = false;
  game_cover_release();
  lv_image_set_src(s_glyph, NULL);
  if (s_sel < GAME_COUNT) {
    const game_t *game = &GAMES[s_sel];
    name = game->name;
    detail = game->detail;
    kind = s_sel < 2 ? "GAME LIBRARY" : "BUILT IN";
    icon = game->icon;
    if (game->target == SCREEN_MUSE)
      embedded = muse_menu_icon();
    if (game->cover)
      cover_loaded = game_cover_show(s_cover, game->cover, s_art_w, s_art_h);
  } else {
    const app_reg_entry_t *app = &s_reg[s_sel - GAME_COUNT];
    name = app->title[0] ? app->title : app->file;
    detail = "Verified on launch";
    kind = "SD APPLICATION";
    if (app->has_icon)
      embedded = &app->icon;
    else
      icon = "/assets/icons/description.bin";
  }
  lv_label_set_text(s_name, name);
  lv_label_set_text(s_detail, detail);
  lv_label_set_text(s_kind, kind);
  lv_obj_add_flag(s_wordmark, LV_OBJ_FLAG_HIDDEN);
  if (cover_loaded) {
    lv_obj_add_flag(s_glyph, LV_OBJ_FLAG_HIDDEN);
  } else if (s_sel < 2) {
    lv_obj_add_flag(s_glyph, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_wordmark, s_sel == 0 ? "GAME\nBOY" : "DOOM");
    lv_obj_center(s_wordmark);
    lv_obj_remove_flag(s_wordmark, LV_OBJ_FLAG_HIDDEN);
  } else {
    const lv_image_dsc_t *descriptor = embedded ? embedded : assets_get(icon);
    if (descriptor && descriptor->header.w && descriptor->header.h) {
      lv_image_set_src(s_glyph, descriptor);
      lv_obj_set_style_image_recolor_opa(s_glyph, embedded ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
      lv_obj_set_style_image_recolor(s_glyph, lv_color_hex(PURPLE), 0);
      int max_side =
          descriptor->header.w > descriptor->header.h ? descriptor->header.w : descriptor->header.h;
      lv_image_set_scale(s_glyph, 96 * 256 / max_side);
      lv_obj_center(s_glyph);
      lv_obj_remove_flag(s_glyph, LV_OBJ_FLAG_HIDDEN);
      lv_anim_delete(s_glyph, fade_icon);
      lv_anim_t fade;
      lv_anim_init(&fade);
      lv_anim_set_var(&fade, s_glyph);
      lv_anim_set_exec_cb(&fade, fade_icon);
      lv_anim_set_values(&fade, 100, 255);
      lv_anim_set_duration(&fade, 180);
      lv_anim_start(&fade);
    } else
      lv_obj_add_flag(s_glyph, LV_OBJ_FLAG_HIDDEN);
  }
}

static void games_menu_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  int count = GAME_COUNT + s_reg_count;
  switch (event->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_MENU);
      break;
    case INPUT_BTN_OK:
      if (press) {
        if (s_sel >= GAME_COUNT)
          app_registry_launch(s_reg[s_sel - GAME_COUNT].file);
        else
          ui_switch_screen(GAMES[s_sel].target);
      }
      break;
    case INPUT_BTN_RIGHT:
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % count;
        ui_feedback(UI_FB_NAV);
        update_view();
      }
      break;
    case INPUT_BTN_LEFT:
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel + count - 1) % count;
        ui_feedback(UI_FB_NAV);
        update_view();
      }
      break;
    default:
      break;
  }
}

void ui_games_menu_open(void) {
  if (s_screen) {
    lv_obj_delete(s_screen);
    s_screen = NULL;
  }
  if (!storage_is_mounted())
    storage_init();
  app_registry_free(s_reg, s_reg_count);
  s_reg_count = app_registry_scan(s_reg, APP_REG_MAX);
  if (s_sel >= GAME_COUNT + s_reg_count)
    s_sel = 0;
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  ui_chrome_header(s_screen, "Apps", NULL);
  ui_chrome_footer(s_screen, "Browse  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT "    OK open    BACK");
  int w = lv_display_get_horizontal_resolution(NULL);
  int h = lv_display_get_vertical_resolution(NULL);
  bool wide = w > h;
  int panel_w = wide ? 144 : w - 24;
  int panel_h = wide ? h - 86 : 158;
  lv_obj_t *panel = lv_obj_create(s_screen);
  lv_obj_set_pos(panel, 12, wide ? 52 : 66);
  lv_obj_set_size(panel, panel_w, panel_h);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x251C35), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x9D83B7), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, 24, 0);
  lv_obj_set_style_bg_opa(panel, 190, 0);
  lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x100C19), 0);
  lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_opa(panel, 90, 0);
  lv_obj_set_style_shadow_color(panel, lv_color_hex(0x783BBA), 0);
  lv_obj_set_style_shadow_width(panel, 18, 0);
  lv_obj_set_style_shadow_opa(panel, 50, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  s_art_w = panel_w - 8;
  s_art_h = panel_h - 8;
  s_cover = game_cover_create(panel);
  s_glyph = lv_image_create(panel);
  lv_image_set_antialias(s_glyph, true);
  s_wordmark = lv_label_create(panel);
  lv_obj_set_style_text_font(s_wordmark, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(s_wordmark, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(s_wordmark, lv_color_hex(PURPLE), 0);
  int text_x = wide ? 168 : 16;
  int text_w = wide ? w - 180 : w - 32;
  s_kind = text_at(s_screen, text_x, wide ? 58 : 46, wide ? text_w : w - 96, PURPLE);
  s_count_label =
      text_at(s_screen, wide ? text_x : w - 74, wide ? 183 : 46, wide ? text_w : 62, MUTED);
  if (!wide)
    lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_RIGHT, 0);
  s_name = text_at(s_screen, text_x, wide ? 82 : 234, text_w, 0xF4EEF8);
  lv_obj_set_style_text_font(s_name, &lv_font_montserrat_16, 0);
  lv_label_set_long_mode(s_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  s_detail = text_at(s_screen, text_x, wide ? 116 : 260, text_w, MUTED);
  lv_obj_add_event_cb(s_screen, release_registry, LV_EVENT_DELETE, NULL);
  update_view();
  ui_input_set_screen_handler(games_menu_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}
