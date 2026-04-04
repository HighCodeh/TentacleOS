#include "theme_selector_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "tos_flash_paths.h"
#include "buttons_gpio.h"
#include "assets_manager.h"
#include "storage_impl.h"
#include "esp_log.h"
#include "cJSON.h"
#include "st7789.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

static const char *TAG = "theme_sel";

/* ── Colors from current_theme (dynamic) ─────────────────────── */
#define T_BORDER()       current_theme.border_interface
#define T_ACCENT()       current_theme.border_accent
#define T_BG_PRI()       current_theme.bg_primary
#define T_BG_SEC()       current_theme.bg_secondary
#define T_TEXT()          current_theme.text_main
#define T_SCREEN()       current_theme.screen_base
#define T_INACTIVE()     current_theme.border_inactive

#define TITLE_W          170
#define TITLE_H          30
#define ITEM_W           210
#define ITEM_H           47
#define OUTER_BORDER     4
#define TOP_BORDER_H     (TITLE_H + 16)
#define SWATCH_SIZE      12
#define SWATCH_GAP       3
#define MAX_THEMES       24

/* ── Theme entry ─────────────────────────────────────────────── */
typedef struct {
  char     name[32];
  uint32_t colors[5]; /* bg_primary, bg_secondary, border_accent, text_main, screen_base */
} theme_entry_t;

/* ── Static state ────────────────────────────────────────────── */
static lv_obj_t   *screen_ts      = NULL;
static lv_obj_t   *items_cont     = NULL;
static lv_obj_t   *items[MAX_THEMES];
static lv_obj_t   *sel_dots[MAX_THEMES];
static lv_obj_t   *active_icons[MAX_THEMES];
static lv_obj_t   *scroll_bar_obj = NULL;
static lv_timer_t *nav_timer      = NULL;

static theme_entry_t themes[MAX_THEMES];
static int theme_count  = 0;
static int selected     = 0;

static int track_y_start = 0;
static int track_h       = 0;

static bool btn_up_last   = false;
static bool btn_down_last = false;
static bool btn_ok_last   = false;
static bool btn_back_last = false;

static bool rebuilding = false;

/* ── Helpers ─────────────────────────────────────────────────── */
static uint32_t hex_str_to_u32(const char *s) {
  if (!s) return 0;
  return (uint32_t)strtol(s, NULL, 16);
}

static char *read_file_alloc(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) { fclose(f); return NULL; }
  char *buf = malloc(sz + 1);
  if (!buf) { fclose(f); return NULL; }
  fread(buf, 1, sz, f);
  buf[sz] = '\0';
  fclose(f);
  return buf;
}

static bool parse_conf_colors(const char *data, uint32_t out[5]) {
  const char *keys[] = {"bg_primary", "bg_secondary", "border_accent", "text_main", "screen_base"};
  int found = 0;
  for (int k = 0; k < 5; k++) {
    const char *p = strstr(data, keys[k]);
    if (!p) continue;
    const char *eq = strchr(p, '=');
    if (!eq) continue;
    eq++;
    while (*eq == ' ') eq++;
    out[k] = hex_str_to_u32(eq);
    found++;
  }
  return found >= 3;
}

static bool parse_json_colors(const char *data, const char *name, uint32_t out[5]) {
  cJSON *root = cJSON_Parse(data);
  if (!root) return false;
  cJSON *theme = cJSON_GetObjectItem(root, name);
  if (!theme) { cJSON_Delete(root); return false; }
  const char *keys[] = {"bg_primary", "bg_secondary", "border_accent", "text_main", "screen_base"};
  for (int k = 0; k < 5; k++) {
    cJSON *v = cJSON_GetObjectItem(theme, keys[k]);
    out[k] = (cJSON_IsString(v) && v->valuestring) ? hex_str_to_u32(v->valuestring) : 0;
  }
  cJSON_Delete(root);
  return true;
}

/* ── Scan themes ─────────────────────────────────────────────── */
static void scan_themes(void) {
  theme_count = 0;

  /* SD themes */
  DIR *d = opendir(TOS_PATH_THEMES);
  if (d) {
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && theme_count < MAX_THEMES) {
      if (ent->d_type != DT_DIR || ent->d_name[0] == '.') continue;
      if (strlen(ent->d_name) > 30) continue;

      char path[96];
      snprintf(path, sizeof(path), TOS_PATH_THEMES "/%.30s/theme.conf", ent->d_name);
      char *data = read_file_alloc(path);
      if (!data) continue;

      theme_entry_t *t = &themes[theme_count];
      strncpy(t->name, ent->d_name, sizeof(t->name) - 1);
      if (parse_conf_colors(data, t->colors)) theme_count++;
      free(data);
    }
    closedir(d);
  }

  /* Flash fallback */
  if (theme_count == 0) {
    char *data = read_file_alloc(FLASH_CONFIG_THEMES);
    if (data) {
      const char *builtin[] = {
        "default","matrix","cyber_blue","blood","toxic","ghost",
        "neon_pink","amber","terminal","ice","deep_purple","midnight"
      };
      for (int i = 0; i < 12 && theme_count < MAX_THEMES; i++) {
        theme_entry_t *t = &themes[theme_count];
        strncpy(t->name, builtin[i], sizeof(t->name) - 1);
        if (parse_json_colors(data, builtin[i], t->colors)) theme_count++;
      }
      free(data);
    }
  }

  /* Find active */
  selected = 0;
  for (int i = 0; i < theme_count; i++) {
    if (strcmp(themes[i].name, g_config_screen.theme) == 0) {
      selected = i;
      break;
    }
  }
}

/* ── Scrollbar animation ─────────────────────────────────────── */
static void update_scroll_bar(void) {
  if (!scroll_bar_obj || theme_count <= 1) return;
  int32_t pos = track_y_start +
      (selected * (track_h - 20)) / (theme_count - 1);

  if (rebuilding) {
    lv_obj_set_y(scroll_bar_obj, pos);
  } else {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scroll_bar_obj);
    lv_anim_set_values(&a, lv_obj_get_y(scroll_bar_obj), pos);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_start(&a);
  }
}

/* ── Selection update ────────────────────────────────────────── */
static void update_selection(void) {
  bool is_active_theme;
  for (int i = 0; i < theme_count; i++) {
    is_active_theme = (strcmp(themes[i].name, g_config_screen.theme) == 0);

    if (i == selected) {
      lv_obj_set_style_border_color(items[i], T_ACCENT(), 0);
      lv_obj_set_style_border_width(items[i], 3, 0);
      if (sel_dots[i]) lv_obj_remove_flag(sel_dots[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_style_border_color(items[i], T_BORDER(), 0);
      lv_obj_set_style_border_width(items[i], 1, 0);
      if (sel_dots[i]) lv_obj_add_flag(sel_dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Active theme checkmark */
    if (active_icons[i]) {
      if (is_active_theme)
        lv_obj_remove_flag(active_icons[i], LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(active_icons[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (items[selected]) {
    lv_obj_scroll_to_view(items[selected], rebuilding ? LV_ANIM_OFF : LV_ANIM_ON);
  }
  update_scroll_bar();
}

/* ── Create one theme item (same look as menu_component_add_item) ── */
static void create_theme_item(lv_obj_t *parent, int idx) {
  theme_entry_t *t = &themes[idx];

  /* Item container — identical to menu_component */
  lv_obj_t *item = lv_obj_create(parent);
  lv_obj_set_size(item, ITEM_W, ITEM_H);
  lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(item, 10, 0);
  lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(item, T_BG_PRI(), 0);
  lv_obj_set_style_bg_grad_color(item, T_BG_SEC(), 0);
  lv_obj_set_style_bg_grad_dir(item, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(item, 1, 0);
  lv_obj_set_style_border_color(item, T_BORDER(), 0);
  lv_obj_set_style_pad_left(item, 6, 0);
  lv_obj_set_style_pad_right(item, 6, 0);
  lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(item, 4, 0);

  /* Color swatches (5 small squares) */
  for (int c = 0; c < 5; c++) {
    lv_obj_t *sw = lv_obj_create(item);
    lv_obj_set_size(sw, SWATCH_SIZE, SWATCH_SIZE);
    lv_obj_set_style_radius(sw, 2, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(t->colors[c]), 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sw, lv_color_hex(t->colors[2]), 0);
    lv_obj_set_style_border_width(sw, 1, 0);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
  }

  /* Theme name label */
  char upper[32];
  strncpy(upper, t->name, sizeof(upper) - 1);
  upper[sizeof(upper) - 1] = '\0';
  for (int c = 0; upper[c]; c++) {
    if (upper[c] >= 'a' && upper[c] <= 'z') upper[c] -= 32;
  }

  lv_obj_t *lbl = lv_label_create(item);
  lv_label_set_text(lbl, upper);
  lv_obj_set_style_text_color(lbl, T_TEXT(), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_flex_grow(lbl, 1);

  /* Active indicator dot (shown only for current theme) */
  lv_obj_t *dot = lv_obj_create(item);
  lv_obj_set_size(dot, 8, 8);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, current_theme.text_main, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(dot, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -12, 0);
  active_icons[idx] = dot;

  /* Selection pointer (same as menu_component) */
  lv_image_dsc_t *pointer_dsc = assets_get("/assets/icons/pointer.bin");
  lv_obj_t *ptr = lv_image_create(item);
  if (pointer_dsc) lv_image_set_src(ptr, pointer_dsc);
  lv_obj_add_flag(ptr, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
  lv_obj_align(ptr, LV_ALIGN_RIGHT_MID, -6, 0);
  sel_dots[idx] = ptr;

  items[idx] = item;

  /* Initial selection styling */
  if (idx == selected) {
    lv_obj_set_style_border_width(item, 3, 0);
    lv_obj_set_style_border_color(item, T_ACCENT(), 0);
    lv_obj_remove_flag(ptr, LV_OBJ_FLAG_HIDDEN);
  }
}

/* ── Apply theme ─────────────────────────────────────────────── */
static void apply_theme(int idx) {
  if (idx < 0 || idx >= theme_count) return;
  ESP_LOGI(TAG, "Applying theme: %s", themes[idx].name);

  ui_theme_load_from_name(themes[idx].name);
  strncpy(g_config_screen.theme, themes[idx].name, sizeof(g_config_screen.theme) - 1);
  tos_config_save(TOS_PATH_CONFIG_SCREEN, "screen");

  /* Rebuild the screen so new colors are visible immediately */
  rebuilding = true;
  ui_theme_selector_open();
  rebuilding = false;
}

/* ── Navigation ──────────────────────────────────────────────── */
static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != screen_ts) {
    lv_timer_delete(t);
    nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked()) return;

  bool up   = up_button_is_down();
  bool down = down_button_is_down();
  bool ok   = ok_button_is_down();
  bool back = back_button_is_down();

  if (up && !btn_up_last && theme_count > 0) {
    selected = (selected == 0) ? theme_count - 1 : selected - 1;
    update_selection();
  }
  if (down && !btn_down_last && theme_count > 0) {
    selected = (selected + 1) % theme_count;
    update_selection();
  }
  if (ok && !btn_ok_last) {
    apply_theme(selected);
  }
  if (back && !btn_back_last) {
    btn_back_last = back;
    ui_switch_screen(SCREEN_INTERFACE_SETTINGS);
    return;
  }

  btn_up_last   = up;
  btn_down_last = down;
  btn_ok_last   = ok;
  btn_back_last = back;
}

/* ── Screen open ─────────────────────────────────────────────── */
void ui_theme_selector_open(void) {
  if (screen_ts) { lv_obj_del(screen_ts); screen_ts = NULL; }

  memset(items, 0, sizeof(items));
  memset(sel_dots, 0, sizeof(sel_dots));
  memset(active_icons, 0, sizeof(active_icons));
  scroll_bar_obj = NULL;

  scan_themes();

  /* ── Root screen (same as menu_component_create) ── */
  screen_ts = lv_obj_create(NULL);
  lv_obj_set_size(screen_ts, LCD_H_RES, LCD_V_RES);
  lv_obj_remove_flag(screen_ts, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen_ts, T_SCREEN(), 0);
  lv_obj_set_style_bg_opa(screen_ts, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen_ts, 0, 0);
  lv_obj_set_style_border_width(screen_ts, OUTER_BORDER, 0);
  lv_obj_set_style_border_color(screen_ts, T_BORDER(), 0);
  lv_obj_set_style_radius(screen_ts, 0, 0);

  /* ── Top area with bottom border ── */
  lv_obj_t *top_area = lv_obj_create(screen_ts);
  lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
  lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, 3, 0);
  lv_obj_set_style_border_color(top_area, T_BORDER(), 0);
  lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);

  /* ── Title pill ── */
  lv_obj_t *title_bar = lv_obj_create(top_area);
  lv_obj_set_size(title_bar, TITLE_W, TITLE_H);
  lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(title_bar, 12, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(title_bar, T_BG_PRI(), 0);
  lv_obj_set_style_bg_grad_color(title_bar, T_BG_SEC(), 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(title_bar, 2, 0);
  lv_obj_set_style_border_color(title_bar, T_ACCENT(), 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);

  lv_obj_t *title_lbl = lv_label_create(title_bar);
  lv_label_set_text(title_lbl, "THEMES");
  lv_obj_set_style_text_color(title_lbl, T_TEXT(), 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(title_lbl);

  /* ── Items container ── */
  int items_y = TOP_BORDER_H + 4;
  int items_h = LCD_V_RES - items_y - OUTER_BORDER - 4;

  items_cont = lv_obj_create(screen_ts);
  lv_obj_set_size(items_cont, ITEM_W + 8, items_h);
  lv_obj_align(items_cont, LV_ALIGN_TOP_LEFT, 4, items_y);
  lv_obj_set_style_bg_opa(items_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(items_cont, 0, 0);
  lv_obj_set_style_pad_all(items_cont, 2, 0);
  lv_obj_set_style_pad_row(items_cont, 6, 0);
  lv_obj_set_flex_flow(items_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(items_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_snap_y(items_cont, LV_SCROLL_SNAP_START);

  /* ── Scroll track + bar ── */
  int track_x = LCD_H_RES - OUTER_BORDER - 9;
  track_y_start = items_y + 10;
  track_h = items_h - 20;

  static lv_point_precise_t track_pts[2];
  track_pts[0].x = 0; track_pts[0].y = 0;
  track_pts[1].x = 0; track_pts[1].y = track_h;

  lv_obj_t *track = lv_line_create(screen_ts);
  lv_line_set_points(track, track_pts, 2);
  lv_obj_set_pos(track, track_x, track_y_start);
  lv_obj_set_style_line_color(track, T_INACTIVE(), 0);
  lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_line_width(track, 3, 0);
  lv_obj_set_style_line_dash_width(track, 4, 0);
  lv_obj_set_style_line_dash_gap(track, 4, 0);

  lv_image_dsc_t *slide_dsc = assets_get("/assets/icons/slide_bar_v.bin");
  scroll_bar_obj = lv_image_create(screen_ts);
  if (slide_dsc) lv_image_set_src(scroll_bar_obj, slide_dsc);
  lv_obj_set_pos(scroll_bar_obj, track_x - 4, track_y_start);
  lv_obj_move_foreground(scroll_bar_obj);

  /* ── Create theme items ── */
  for (int i = 0; i < theme_count; i++) {
    create_theme_item(items_cont, i);
  }
  update_selection();

  /* ── Nav timer ── */
  if (nav_timer == NULL) {
    nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
  }

  lv_screen_load(screen_ts);
}
