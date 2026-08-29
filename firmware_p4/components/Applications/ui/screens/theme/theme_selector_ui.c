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

#include "theme_selector_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "assets_manager.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "page_dots_ui.h"
#include "reboot_ui.h"
#include "st7789.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "THEME_SELECTOR_UI";

#define TITLE_ICON          "/assets/icons/palette.bin"
#define BASE_FRAME          "/assets/frames/base_frame_0.bin"
#define CARD_Y_BIAS         (-18)
#define ANIM_MS             220
#define THEMES_DIR          "/sdcard/themes"
#define MAX_THEMES          24
#define TNAME_MAX           32
#define THEME_PATH_MAX      128
#define ACCENT_BUF_SIZE     1024
#define DEFAULT_CARD_ACCENT 0x888888
#define THEME_FILE_JSON     "theme.json"
#define THEME_FILE_CONF     "theme.conf"
#define ACCENT_KEY          "border_accent"
#define HEX_PREFIX          "0x"
#define HEX_BASE            16

extern int theme_idx;
extern const char *theme_names[];

typedef struct {
  const char *label;
  uint32_t accent;
} theme_face_t;

static const theme_face_t THEMES[] = {
    {"Default", 0x834EC6},
    {"Cyber Blue", 0x00D9FF},
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

typedef struct {
  char name[TNAME_MAX];
  char label[TNAME_MAX];
  uint32_t accent;
  bool builtin;
  int flash_idx;
} theme_entry_t;

static const int32_t CAR_PX[] = {-94, -50, 0, 50, 94};
static const int32_t CAR_PY[] = {-14, -6, 0, -6, -14};
static const int32_t CAR_SC[] = {117, 161, 234, 161, 117};
static const int32_t CAR_OP[] = {LV_OPA_50, LV_OPA_80, LV_OPA_COVER, LV_OPA_80, LV_OPA_50};
static const int32_t CAR_Z[] = {0, 1, 2, 1, 0};
#define CAR_SLOTS  5
#define CAR_CENTER 2

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_cards[MAX_THEMES];
static lv_obj_t *s_label = NULL;
static lv_obj_t *s_active = NULL;
static page_dots_t s_dots;
static lv_image_dsc_t *s_base_dsc = NULL;
static theme_entry_t s_entries[MAX_THEMES];
static int s_count = 0;
static int s_sel = 0;
static int s_applied = 0;
static char s_readbuf[ACCENT_BUF_SIZE];
static bool s_animating = false;

static void build_screen(void);

// Animation exec wrappers (explicit, so we avoid the cast-function-type idiom).
static void anim_set_x(void *o, int32_t v) {
  lv_obj_set_x((lv_obj_t *)o, v);
}
static void anim_set_y(void *o, int32_t v) {
  lv_obj_set_y((lv_obj_t *)o, v);
}
static void anim_set_scale(void *o, int32_t v) {
  lv_image_set_scale((lv_obj_t *)o, (uint32_t)v);
}
static void anim_set_opa(void *o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
}
static void anim_done_cb(lv_anim_t *a) {
  (void)a;
  s_animating = false;
}

static bool has_theme_file(const char *name) {
  char path[THEME_PATH_MAX];
  struct stat st;
  snprintf(path, sizeof(path), "%s/%s/%s", THEMES_DIR, name, THEME_FILE_JSON);
  if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
    return true;
  snprintf(path, sizeof(path), "%s/%s/%s", THEMES_DIR, name, THEME_FILE_CONF);
  return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static uint32_t read_sd_accent(const char *name) {
  char path[THEME_PATH_MAX];
  for (int variant = 0; variant < 2; variant++) {
    snprintf(path,
             sizeof(path),
             "%s/%s/%s",
             THEMES_DIR,
             name,
             variant == 0 ? THEME_FILE_JSON : THEME_FILE_CONF);
    FILE *f = fopen(path, "rb");
    if (f == NULL)
      continue;
    size_t n = fread(s_readbuf, 1, sizeof(s_readbuf) - 1, f);
    fclose(f);
    s_readbuf[n] = '\0';
    uint32_t accent = DEFAULT_CARD_ACCENT;
    char *p = strstr(s_readbuf, ACCENT_KEY);
    if (p != NULL) {
      char *h = strstr(p, HEX_PREFIX);
      if (h != NULL)
        accent = (uint32_t)strtoul(h, NULL, HEX_BASE);
    }
    return accent;
  }
  return DEFAULT_CARD_ACCENT;
}

static void build_entries(void) {
  s_count = 0;

  for (int i = 0; i < THEME_COUNT && s_count < MAX_THEMES; i++) {
    theme_entry_t *e = &s_entries[s_count++];
    strlcpy(e->name, theme_names[i], sizeof(e->name));
    strlcpy(e->label, THEMES[i].label, sizeof(e->label));
    e->accent = THEMES[i].accent;
    e->builtin = true;
    e->flash_idx = i;
  }

  DIR *dir = opendir(THEMES_DIR);
  if (dir != NULL) {
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && s_count < MAX_THEMES) {
      if (de->d_name[0] == '.')
        continue;
      bool dup = false;
      for (int i = 0; i < THEME_COUNT; i++)
        if (strcmp(de->d_name, theme_names[i]) == 0)
          dup = true;
      if (dup || !has_theme_file(de->d_name))
        continue;
      theme_entry_t *e = &s_entries[s_count++];
      strlcpy(e->name, de->d_name, sizeof(e->name));
      strlcpy(e->label, de->d_name, sizeof(e->label));
      e->accent = read_sd_accent(de->d_name);
      e->builtin = false;
      e->flash_idx = -1;
    }
    closedir(dir);
  }

  if (s_count == 0) {
    strlcpy(s_entries[0].name, theme_names[0], sizeof(s_entries[0].name));
    strlcpy(s_entries[0].label, THEMES[0].label, sizeof(s_entries[0].label));
    s_entries[0].accent = THEMES[0].accent;
    s_entries[0].builtin = true;
    s_entries[0].flash_idx = 0;
    s_count = 1;
  }
}

static int find_applied(void) {
  for (int i = 0; i < s_count; i++)
    if (strcmp(s_entries[i].name, g_config_screen.theme) == 0)
      return i;
  return 0;
}

static int32_t carousel_slot(int item_idx) {
  int32_t n = s_count;
  int32_t d = (item_idx - s_sel + n) % n;
  if (d > n / 2)
    d -= n;
  int32_t slot = CAR_CENTER + d;
  return (slot >= 0 && slot < CAR_SLOTS) ? slot : -1;
}

static lv_obj_t *make_card(lv_obj_t *parent, int i) {
  lv_obj_t *card = lv_image_create(parent);
  if (s_base_dsc)
    lv_image_set_src(card, s_base_dsc);
  lv_image_set_antialias(card, false);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_BIAS);
  lv_obj_set_style_image_recolor(card, lv_color_hex(s_entries[i].accent), 0);
  lv_obj_set_style_image_recolor_opa(card, LV_OPA_COVER, 0);
  return card;
}

static void place_card(int i, bool anim) {
  lv_obj_t *card = s_cards[i];
  if (card == NULL)
    return;
  int32_t slot = carousel_slot(i);

  if (slot < 0) {
    lv_obj_set_style_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);

  int32_t tx = CAR_PX[slot];
  int32_t ty = CAR_PY[slot] + CARD_Y_BIAS;
  int32_t ts = CAR_SC[slot];
  int32_t to = CAR_OP[slot];

  if (!anim) {
    lv_obj_align(card, LV_ALIGN_CENTER, tx, ty);
    lv_image_set_scale(card, ts);
    lv_obj_set_style_opa(card, to, 0);
    return;
  }

  // Slide/scale/fade toward the target slot (mirrors the shipped menu_ui pattern:
  // decoded frames are cached, so this no longer re-decodes per navigation).
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_duration(&a, ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_var(&a, card);

  lv_anim_set_values(&a, lv_obj_get_x_aligned(card), tx);
  lv_anim_set_exec_cb(&a, anim_set_x);
  if (slot == CAR_CENTER)
    lv_anim_set_completed_cb(&a, anim_done_cb); // one card clears the guard
  lv_anim_start(&a);
  lv_anim_set_completed_cb(&a, NULL);

  lv_anim_set_values(&a, lv_obj_get_y_aligned(card), ty);
  lv_anim_set_exec_cb(&a, anim_set_y);
  lv_anim_start(&a);

  lv_anim_set_values(&a, lv_image_get_scale(card), ts);
  lv_anim_set_exec_cb(&a, anim_set_scale);
  lv_anim_start(&a);

  lv_anim_set_values(&a, lv_obj_get_style_opa(card, 0), to);
  lv_anim_set_exec_cb(&a, anim_set_opa);
  lv_anim_start(&a);
}

static void fix_z_order(void) {
  for (int z = 0; z <= CAR_CENTER; z++) {
    for (int i = 0; i < s_count; i++) {
      int32_t slot = carousel_slot(i);
      if (slot >= 0 && CAR_Z[slot] == z)
        lv_obj_move_foreground(s_cards[i]);
    }
  }
}

static void update_view(bool anim) {
  lv_label_set_text_fmt(s_label, LV_SYMBOL_LEFT "  %s  " LV_SYMBOL_RIGHT, s_entries[s_sel].label);
  lv_label_set_text(s_active, s_sel == s_applied ? LV_SYMBOL_OK " APPLIED" : "OK to apply");
  lv_obj_set_style_text_color(
      s_active, s_sel == s_applied ? current_theme.border_accent : current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_active, s_sel == s_applied ? LV_OPA_COVER : LV_OPA_50, 0);

  page_dots_set(&s_dots, s_sel);
  for (int i = 0; i < s_count; i++)
    place_card(i, anim);
  fix_z_order();
}

static bool s_is_reboot_armed = false;

static void theme_reboot_cb(bool confirm) {
  (void)confirm;
  msgbox_close();
  reboot_ui_reboot();
}

static void theme_selector_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_SETTINGS);
      break;
    case INPUT_BTN_OK:
      if (press && !s_is_reboot_armed && s_sel != s_applied) {
        theme_entry_t *e = &s_entries[s_sel];
        if (!ui_sd_ready())
          break;
        strlcpy(g_config_screen.theme, e->name, sizeof(g_config_screen.theme));
        tos_config_save(TOS_PATH_CONFIG_SCREEN, "screen");
        s_is_reboot_armed = true;
        ESP_LOGI(TAG, "theme %d (%s) saved; restarting to apply", s_sel, e->name);
        msgbox_open("/assets/icons/restart_alt.bin",
                    "Theme saved. Restarting to apply it.",
                    "RESTART",
                    NULL,
                    theme_reboot_cb);
      }
      break;
    case INPUT_BTN_RIGHT:
    case INPUT_BTN_DOWN:
      if (nav && !s_animating) {
        s_sel = (s_sel + 1) % s_count;
        s_animating = true;
        ui_feedback(UI_FB_NAV);
        update_view(true);
      }
      break;
    case INPUT_BTN_LEFT:
    case INPUT_BTN_UP:
      if (nav && !s_animating) {
        s_sel = (s_sel == 0) ? s_count - 1 : s_sel - 1;
        s_animating = true;
        ui_feedback(UI_FB_NAV);
        update_view(true);
      }
      break;
    default:
      break;
  }
}

static void build_screen(void) {
  lv_obj_t *prev = s_screen;
  s_animating = false;

  if (s_base_dsc == NULL)
    s_base_dsc = assets_get(BASE_FRAME);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "THEME", TITLE_ICON);
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " Browse   " LV_SYMBOL_OK " Apply");

  for (int i = 0; i < s_count; i++)
    s_cards[i] = make_card(s_screen, i);

  s_label = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_label, current_theme.border_accent, 0);
  lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 52);

  s_active = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_active, &lv_font_montserrat_12, 0);
  lv_obj_align(s_active, LV_ALIGN_BOTTOM_MID, 0, -42);

  s_dots = page_dots_create(s_screen, s_count, LV_ALIGN_BOTTOM_MID, 0, -26);

  if (s_sel < 0)
    s_sel = 0;
  if (s_sel >= s_count)
    s_sel = s_count - 1;
  update_view(false);

  ui_input_set_screen_handler(theme_selector_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
  if (prev != NULL)
    lv_obj_del(prev);
}

void ui_theme_selector_open(void) {
  s_is_reboot_armed = false;
  build_entries();
  s_applied = find_applied();
  s_sel = s_applied;
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  build_screen();
}
