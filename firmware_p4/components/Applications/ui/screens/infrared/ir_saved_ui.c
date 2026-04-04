// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ir_saved_ui.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>

#include "esp_log.h"

#include "ui_theme.h"
#include "ui_manager.h"
#include "buttons_gpio.h"
#include "assets_manager.h"
#include "text_viewer_ui.h"
#include "tos_storage_paths.h"
#include "st7789.h"

static const char *TAG = "IR_BROWSE_UI";

#define OUTER_BORDER   4
#define TOP_BORDER_H   46
#define ITEM_H         47
#define ITEM_W         210
#define MAX_ENTRIES    24

typedef enum {
    BROWSE_LEVEL_PROTOCOLS = 0,
    BROWSE_LEVEL_FILES,
} browse_level_t;

/* ── Screen objects ──────────────────────────────────────────── */
static lv_obj_t   *s_screen     = NULL;
static lv_timer_t *s_nav_timer  = NULL;
static lv_obj_t   *s_items_cont = NULL;
static lv_obj_t   *s_item_objs[MAX_ENTRIES];
static lv_obj_t   *s_scroll_bar = NULL;
static lv_obj_t   *s_title_lbl  = NULL;

static char s_entries[MAX_ENTRIES][64];
static int  s_entry_count = 0;
static int  s_selected    = 0;

static int s_track_y_start;
static int s_track_h;

/* ── Navigation state ────────────────────────────────────────── */
static browse_level_t s_level = BROWSE_LEVEL_PROTOCOLS;
static char s_current_proto[32];

/* ── Viewer state ────────────────────────────────────────────── */
static bool          s_is_viewing = false;
static text_viewer_t s_viewer;

static bool s_btn_up_last   = false;
static bool s_btn_down_last = false;
static bool s_btn_ok_last   = false;
static bool s_btn_back_last = false;

/* ── Forward declarations ────────────────────────────────────── */
static void build_list(void);

/* ── Helpers ─────────────────────────────────────────────────── */
static void update_scroll_bar(void) {
    if (s_scroll_bar == NULL || s_entry_count <= 1) return;
    int32_t pos = s_track_y_start +
        (s_selected * (s_track_h - 20)) / (s_entry_count - 1);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_scroll_bar);
    lv_anim_set_values(&a, lv_obj_get_y(s_scroll_bar), pos);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_start(&a);
}

static void update_selection(void) {
    for (int i = 0; i < s_entry_count; i++) {
        if (i == s_selected) {
            lv_obj_set_style_border_width(s_item_objs[i], 3, 0);
            lv_obj_set_style_border_color(s_item_objs[i], current_theme.border_accent, 0);
        } else {
            lv_obj_set_style_border_width(s_item_objs[i], 1, 0);
            lv_obj_set_style_border_color(s_item_objs[i], current_theme.border_interface, 0);
        }
    }
    if (s_entry_count > 0 && s_item_objs[s_selected] != NULL) {
        lv_obj_scroll_to_view(s_item_objs[s_selected], LV_ANIM_ON);
    }
    update_scroll_bar();
}

static void scan_protocols(void) {
    s_entry_count = 0;
    DIR *d = opendir(TOS_PATH_IR);
    if (d == NULL) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_entry_count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        if (ent->d_type != DT_DIR) continue;
        strncpy(s_entries[s_entry_count], ent->d_name, 63);
        s_entries[s_entry_count][63] = '\0';
        s_entry_count++;
    }
    closedir(d);
}

static void scan_files(const char *proto) {
    s_entry_count = 0;
    char dir_path[300];
    snprintf(dir_path, sizeof(dir_path), TOS_PATH_IR "/%.64s", proto);

    DIR *d = opendir(dir_path);
    if (d == NULL) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_entry_count < MAX_ENTRIES) {
        size_t len = strlen(ent->d_name);
        if (len < 4 || strcmp(ent->d_name + len - 3, ".ir") != 0) continue;
        strncpy(s_entries[s_entry_count], ent->d_name, 63);
        s_entries[s_entry_count][63] = '\0';
        s_entry_count++;
    }
    closedir(d);
}

static lv_obj_t *create_item(lv_obj_t *parent, const char *text, const char *icon_sym) {
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_set_size(item, ITEM_W, ITEM_H);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(item, 10, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_color(item, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(item, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, current_theme.border_interface, 0);
    lv_obj_set_style_pad_left(item, 8, 0);
    lv_obj_set_style_pad_right(item, 8, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 6, 0);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *arrow = lv_label_create(item);
    lv_label_set_text(arrow, icon_sym);
    lv_obj_set_style_text_color(arrow, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_12, 0);

    return item;
}

static void build_list(void) {
    if (s_items_cont != NULL) lv_obj_clean(s_items_cont);

    if (s_level == BROWSE_LEVEL_PROTOCOLS) {
        scan_protocols();
        if (s_title_lbl != NULL) lv_label_set_text(s_title_lbl, "BROWSE SIGNALS");
    } else {
        scan_files(s_current_proto);
        if (s_title_lbl != NULL) lv_label_set_text(s_title_lbl, s_current_proto);
    }

    const char *icon = (s_level == BROWSE_LEVEL_PROTOCOLS)
                       ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_EYE_OPEN;

    for (int i = 0; i < s_entry_count; i++) {
        s_item_objs[i] = create_item(s_items_cont, s_entries[i], icon);
    }

    if (s_entry_count == 0) {
        lv_obj_t *empty = lv_label_create(s_items_cont);
        lv_label_set_text(empty,
            s_level == BROWSE_LEVEL_PROTOCOLS ? "No protocols found" : "No signals");
        lv_obj_set_style_text_color(empty, current_theme.border_inactive, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
    }

    s_selected = 0;
    update_selection();
}

/* ── Viewer ──────────────────────────────────────────────────── */
static void view_selected(void) {
    if (s_entry_count == 0) return;

    char path[300];
    snprintf(path, sizeof(path), TOS_PATH_IR "/%.64s/%.64s",
             s_current_proto, s_entries[s_selected]);

    s_viewer = text_viewer_create(s_screen, s_entries[s_selected]);
    text_viewer_load_file(&s_viewer, path);

    lv_obj_move_foreground(s_viewer.screen);
    s_is_viewing = true;
}

static void close_viewer(void) {
    if (s_viewer.screen != NULL) {
        lv_obj_del(s_viewer.screen);
        s_viewer.screen = NULL;
    }
    s_is_viewing = false;
}

/* ── Navigation ──────────────────────────────────────────────── */
static void nav_timer_cb(lv_timer_t *t) {
    if (lv_screen_active() != s_screen) {
        lv_timer_delete(t);
        s_nav_timer = NULL;
        return;
    }
    if (ui_input_is_locked()) return;

    bool up   = up_button_is_down();
    bool down = down_button_is_down();
    bool ok   = ok_button_is_down();
    bool back = back_button_is_down();

    if (s_is_viewing) {
        if (up && !s_btn_up_last && s_viewer.text_area != NULL) {
            if (lv_obj_get_scroll_y(s_viewer.text_area) > 0)
                lv_obj_scroll_by(s_viewer.text_area, 0, 30, LV_ANIM_ON);
        }
        if (down && !s_btn_down_last && s_viewer.text_area != NULL) {
            lv_obj_scroll_by(s_viewer.text_area, 0, -30, LV_ANIM_ON);
        }
        if (back && !s_btn_back_last) {
            close_viewer();
        }
    } else {
        if (down && !s_btn_down_last && s_entry_count > 0) {
            s_selected = (s_selected + 1) % s_entry_count;
            update_selection();
        }
        if (up && !s_btn_up_last && s_entry_count > 0) {
            s_selected = (s_selected == 0) ? s_entry_count - 1 : s_selected - 1;
            update_selection();
        }
        if (ok && !s_btn_ok_last && s_entry_count > 0) {
            if (s_level == BROWSE_LEVEL_PROTOCOLS) {
                strncpy(s_current_proto, s_entries[s_selected],
                        sizeof(s_current_proto) - 1);
                s_level = BROWSE_LEVEL_FILES;
                build_list();
            } else {
                view_selected();
            }
        }
        if (back && !s_btn_back_last) {
            if (s_level == BROWSE_LEVEL_FILES) {
                s_level = BROWSE_LEVEL_PROTOCOLS;
                build_list();
            } else {
                ui_switch_screen(SCREEN_IR_MENU);
            }
        }
    }

    s_btn_up_last   = up;
    s_btn_down_last = down;
    s_btn_ok_last   = ok;
    s_btn_back_last = back;
}

/* ── Screen open ─────────────────────────────────────────────── */
void ui_ir_saved_open(void) {
    if (s_screen != NULL) { lv_obj_del(s_screen); s_screen = NULL; }

    s_selected = 0;
    s_level = BROWSE_LEVEL_PROTOCOLS;
    s_is_viewing = false;
    memset(&s_viewer, 0, sizeof(s_viewer));

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_screen, OUTER_BORDER, 0);
    lv_obj_set_style_border_color(s_screen, current_theme.border_interface, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    /* Top area */
    lv_obj_t *top_area = lv_obj_create(s_screen);
    lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
    lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_area, 3, 0);
    lv_obj_set_style_border_color(top_area, current_theme.border_interface, 0);
    lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(top_area, 0, 0);
    lv_obj_set_style_pad_all(top_area, 0, 0);

    /* Title pill */
    lv_obj_t *title_bar = lv_obj_create(top_area);
    lv_obj_set_size(title_bar, 170, 30);
    lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_bar, 12, 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(title_bar, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(title_bar, 2, 0);
    lv_obj_set_style_border_color(title_bar, current_theme.border_accent, 0);

    s_title_lbl = lv_label_create(title_bar);
    lv_label_set_text(s_title_lbl, "BROWSE SIGNALS");
    lv_obj_set_style_text_color(s_title_lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(s_title_lbl);

    /* Items container */
    int items_y = TOP_BORDER_H + 4;
    int items_h = LCD_V_RES - items_y - OUTER_BORDER - 4;

    s_items_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_items_cont, ITEM_W + 8, items_h);
    lv_obj_align(s_items_cont, LV_ALIGN_TOP_LEFT, 4, items_y);
    lv_obj_set_style_bg_opa(s_items_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_items_cont, 0, 0);
    lv_obj_set_style_pad_all(s_items_cont, 2, 0);
    lv_obj_set_style_pad_row(s_items_cont, 6, 0);
    lv_obj_set_flex_flow(s_items_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_items_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(s_items_cont, LV_SCROLL_SNAP_START);

    /* Scroll track + bar */
    int track_x = LCD_H_RES - OUTER_BORDER - 10;
    s_track_y_start = items_y + 10;
    s_track_h = items_h - 20;

    static lv_point_precise_t track_pts[2];
    track_pts[0].x = 0; track_pts[0].y = 0;
    track_pts[1].x = 0; track_pts[1].y = s_track_h;

    lv_obj_t *track = lv_line_create(s_screen);
    lv_line_set_points(track, track_pts, 2);
    lv_obj_set_pos(track, track_x, s_track_y_start);
    lv_obj_set_style_line_color(track, current_theme.border_inactive, 0);
    lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_line_width(track, 3, 0);
    lv_obj_set_style_line_dash_width(track, 4, 0);
    lv_obj_set_style_line_dash_gap(track, 4, 0);

    static lv_image_dsc_t *sb_dsc = NULL;
    if (sb_dsc == NULL) sb_dsc = assets_get("/assets/icons/slide_bar_v.bin");
    s_scroll_bar = lv_image_create(s_screen);
    if (sb_dsc != NULL) lv_image_set_src(s_scroll_bar, sb_dsc);
    lv_obj_set_pos(s_scroll_bar, track_x - 4, s_track_y_start);
    lv_obj_move_foreground(s_scroll_bar);

    build_list();

    if (s_nav_timer == NULL) {
        s_nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    lv_screen_load(s_screen);
}
