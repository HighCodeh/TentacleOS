#include "ui_theme.h"
#include "files_ui.h"
#include "text_viewer_ui.h"
#include "ui_manager.h"
#include "buttons_gpio.h"
#include "assets_manager.h"
#include "storage_assets.h"
#include "st7789.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define BORDER_COLOR     current_theme.border_interface
#define ITEM_BORDER      current_theme.border_accent
#define GRAD_LEFT        current_theme.border_interface
#define GRAD_RIGHT       current_theme.bg_secondary
#define SEL_BORDER       current_theme.border_accent
#define OUTER_BORDER     4
#define TOP_BORDER_H     46
#define ITEM_H           50
#define ITEM_W           210
#define MAX_ENTRIES      20

static const char *TAG = "FILES_UI";

static lv_obj_t * screen_files = NULL;
static lv_timer_t * nav_timer = NULL;

static lv_obj_t * path_label = NULL;
static lv_obj_t * items_cont = NULL;
static lv_obj_t * item_objs[MAX_ENTRIES];
static lv_obj_t * scroll_bar = NULL;

static char current_path[256] = "/assets";
static char entry_names[MAX_ENTRIES][64];
static bool entry_is_dir[MAX_ENTRIES];
static int entry_count = 0;
static int selected = 0;
static bool viewing_file = false;
static text_viewer_t viewer;

static bool btn_up_last = false;
static bool btn_down_last = false;
static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_back_last = false;

static lv_font_t * file_font = NULL;

static int track_y_start;
static int track_h;

static void update_scroll_bar(void) {
    if (!scroll_bar || entry_count <= 1) return;
    int32_t pos = track_y_start +
        (selected * (track_h - 20)) / (entry_count - 1);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scroll_bar);
    lv_anim_set_values(&a, lv_obj_get_y(scroll_bar), pos);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_start(&a);
}

static void update_selection(void) {
    for (int i = 0; i < entry_count; i++) {
        if (i == selected) {
            lv_obj_set_style_border_width(item_objs[i], 2, 0);
            lv_obj_set_style_border_color(item_objs[i], SEL_BORDER, 0);
        } else {
            lv_obj_set_style_border_width(item_objs[i], 1, 0);
            lv_obj_set_style_border_color(item_objs[i], ITEM_BORDER, 0);
        }
    }
    if (entry_count > 0 && item_objs[selected]) {
        lv_obj_scroll_to_view(item_objs[selected], LV_ANIM_ON);
    }
    update_scroll_bar();
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void scan_directory(void) {
    entry_count = 0;
    DIR * dir = opendir(current_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open: %s", current_path);
        return;
    }

    struct dirent * ent;
    while ((ent = readdir(dir)) != NULL && entry_count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;
        strncpy(entry_names[entry_count], ent->d_name, 63);
        entry_names[entry_count][63] = '\0';

        char full[384];
        snprintf(full, sizeof(full), "%s/%s", current_path, ent->d_name);
        struct stat st;
        entry_is_dir[entry_count] = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));

        entry_count++;
    }
    closedir(dir);
}
#pragma GCC diagnostic pop

static void build_file_list(void);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void open_file_viewer(void) {
    char full_path[384];
    snprintf(full_path, sizeof(full_path), "%s/%s", current_path, entry_names[selected]);

    viewer = text_viewer_create(screen_files, entry_names[selected]);
    text_viewer_load_file(&viewer, full_path);
    lv_obj_move_foreground(viewer.screen);
    viewing_file = true;
}

static void close_file_viewer(void) {
    if (viewer.screen) {
        lv_obj_del(viewer.screen);
        viewer.screen = NULL;
    }
    viewing_file = false;
}

static void navigate_into(void) {
    if (entry_count == 0) return;
    if (!entry_is_dir[selected]) {
        open_file_viewer();
        return;
    }
    char new_path[256];
    snprintf(new_path, sizeof(new_path), "%s/%s", current_path, entry_names[selected]);
    strncpy(current_path, new_path, sizeof(current_path) - 1);
    selected = 0;
    build_file_list();
}
#pragma GCC diagnostic pop

static void navigate_back(void) {
    char * last = strrchr(current_path, '/');
    if (last && last != current_path) {
        *last = '\0';
        selected = 0;
        build_file_list();
    }
}

static void build_file_list(void) {
    if (items_cont) {
        lv_obj_clean(items_cont);
    }

    scan_directory();

    if (path_label) {
        lv_label_set_text(path_label, current_path);
    }

    static lv_image_dsc_t * folder_dsc = NULL;
    static lv_image_dsc_t * file_dsc = NULL;
    if (!folder_dsc) folder_dsc = assets_get("/assets/frames/folder_frame_0.bin");
    if (!file_dsc) file_dsc = assets_get("/assets/frames/file_frame_0.bin");

    for (int i = 0; i < entry_count; i++) {
        lv_obj_t * item = lv_obj_create(items_cont);
        lv_obj_set_size(item, ITEM_W, ITEM_H);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(item, 10, 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(item, GRAD_LEFT, 0);
        lv_obj_set_style_bg_grad_color(item, GRAD_RIGHT, 0);
        lv_obj_set_style_bg_grad_dir(item, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_border_color(item, ITEM_BORDER, 0);
        lv_obj_set_style_pad_left(item, 8, 0);
        lv_obj_set_style_pad_right(item, 8, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 6, 0);

        lv_image_dsc_t * dsc = entry_is_dir[i] ? folder_dsc : file_dsc;
        if (dsc) {
            lv_obj_t * icon = lv_image_create(item);
            lv_image_set_src(icon, dsc);
            lv_image_set_scale(icon, 128);
        }

        lv_obj_t * lbl = lv_label_create(item);
        lv_label_set_text(lbl, entry_names[i]);
        lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_flex_grow(lbl, 1);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

        if (entry_is_dir[i]) {
            lv_obj_t * arrow = lv_label_create(item);
            lv_label_set_text(arrow, LV_SYMBOL_REFRESH);
            lv_obj_set_style_text_color(arrow, current_theme.border_accent, 0);
            lv_obj_set_style_text_font(arrow, &lv_font_montserrat_12, 0);
        }

        item_objs[i] = item;
    }

    if (entry_count == 0) {
        lv_obj_t * empty = lv_label_create(items_cont);
        lv_label_set_text(empty, "Empty folder");
        lv_obj_set_style_text_color(empty, current_theme.border_inactive, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
    }

    update_selection();
}

static void nav_timer_cb(lv_timer_t * t) {
    if (lv_screen_active() != screen_files) {
        lv_timer_delete(t);
        nav_timer = NULL;
        return;
    }
    if (ui_input_is_locked()) return;

    bool up    = up_button_is_down();
    bool down  = down_button_is_down();
    bool left  = left_button_is_down();
    bool right = right_button_is_down();
    bool back  = back_button_is_down();

    if (viewing_file) {
        if (down && !btn_down_last && viewer.text_area) {
            lv_obj_scroll_by(viewer.text_area, 0, -30, LV_ANIM_ON);
        }
        if (up && !btn_up_last && viewer.text_area) {
            lv_obj_scroll_by(viewer.text_area, 0, 30, LV_ANIM_ON);
        }
        if ((left && !btn_left_last) || (back && !btn_back_last)) {
            close_file_viewer();
        }
        btn_up_last = up; btn_down_last = down;
        btn_left_last = left; btn_right_last = right; btn_back_last = back;
        return;
    }

    if (down && !btn_down_last && entry_count > 0) {
        selected = (selected + 1) % entry_count;
        update_selection();
    }
    if (up && !btn_up_last && entry_count > 0) {
        selected = (selected == 0) ? entry_count - 1 : selected - 1;
        update_selection();
    }
    if (right && !btn_right_last) {
        navigate_into();
    }
    if (left && !btn_left_last) {
        navigate_back();
    }
    if (back && !btn_back_last) {
        ui_switch_screen(SCREEN_MENU);
        return;
    }

    btn_up_last    = up;
    btn_down_last  = down;
    btn_left_last  = left;
    btn_right_last = right;
    btn_back_last  = back;
}

void ui_files_open(void) {
    if (screen_files) { lv_obj_del(screen_files); screen_files = NULL; }
    screen_files = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_files, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(screen_files, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_files, LV_OBJ_FLAG_SCROLLABLE);

    if (!file_font) {
        extern lv_font_t * lv_binfont_create(const char *);
        file_font = lv_binfont_create("A:assets/fonts/Inter.bin");
    }

    lv_obj_set_style_border_width(screen_files, OUTER_BORDER, 0);
    lv_obj_set_style_border_color(screen_files, BORDER_COLOR, 0);
    lv_obj_set_style_radius(screen_files, 0, 0);
    lv_obj_set_style_pad_all(screen_files, 0, 0);

    lv_obj_t * top_area = lv_obj_create(screen_files);
    lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
    lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_area, 3, 0);
    lv_obj_set_style_border_color(top_area, BORDER_COLOR, 0);
    lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(top_area, 0, 0);
    lv_obj_set_style_pad_all(top_area, 0, 0);

    lv_obj_t * title_bar = lv_obj_create(top_area);
    lv_obj_set_size(title_bar, 170, 30);
    lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_bar, 12, 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(title_bar, GRAD_LEFT, 0);
    lv_obj_set_style_bg_grad_color(title_bar, GRAD_RIGHT, 0);
    lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(title_bar, 2, 0);
    lv_obj_set_style_border_color(title_bar, ITEM_BORDER, 0);

    static lv_image_dsc_t * folder_icon = NULL;
    if (!folder_icon) folder_icon = assets_get("/assets/frames/folder_frame_0.bin");
    if (folder_icon) {
        lv_obj_t * ti = lv_image_create(title_bar);
        lv_image_set_src(ti, folder_icon);
        lv_obj_add_flag(ti, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(ti, LV_ALIGN_LEFT_MID, 8, 0);
        lv_image_set_scale(ti, 80);
    }

    lv_obj_t * title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, "Files");
    lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(title_lbl, file_font ? file_font : &lv_font_montserrat_14, 0);
    lv_obj_center(title_lbl);

    int content_y = TOP_BORDER_H + 4;
    path_label = lv_label_create(screen_files);
    lv_label_set_text(path_label, current_path);
    lv_obj_set_style_text_color(path_label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(path_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(path_label, LCD_H_RES - OUTER_BORDER * 2 - 30);
    lv_label_set_long_mode(path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(path_label, LV_ALIGN_TOP_MID, -8, content_y + 4);

    int items_y = content_y + 28;
    int items_h = LCD_V_RES - items_y - OUTER_BORDER - 4;

    items_cont = lv_obj_create(screen_files);
    lv_obj_set_size(items_cont, ITEM_W + 8, items_h);
    lv_obj_align(items_cont, LV_ALIGN_TOP_LEFT, 4, items_y);
    lv_obj_set_style_bg_opa(items_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(items_cont, 0, 0);
    lv_obj_set_style_pad_all(items_cont, 2, 0);
    lv_obj_set_style_pad_row(items_cont, 6, 0);
    lv_obj_set_flex_flow(items_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(items_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(items_cont, LV_SCROLL_SNAP_START);

    int track_x = LCD_H_RES - OUTER_BORDER - 10;
    track_y_start = items_y + 10;
    track_h = items_h - 20;

    static lv_point_precise_t track_pts[2];
    track_pts[0].x = 0; track_pts[0].y = 0;
    track_pts[1].x = 0; track_pts[1].y = track_h;

    lv_obj_t * track = lv_line_create(screen_files);
    lv_line_set_points(track, track_pts, 2);
    lv_obj_set_pos(track, track_x, track_y_start);
    lv_obj_set_style_line_color(track, current_theme.text_main, 0);
    lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_line_width(track, 3, 0);
    lv_obj_set_style_line_dash_width(track, 4, 0);
    lv_obj_set_style_line_dash_gap(track, 4, 0);

    static lv_image_dsc_t * sb_dsc = NULL;
    if (!sb_dsc) sb_dsc = assets_get("/assets/icons/slide_bar_v.bin");
    scroll_bar = lv_image_create(screen_files);
    if (sb_dsc) lv_image_set_src(scroll_bar, sb_dsc);
    lv_obj_set_pos(scroll_bar, track_x - 4, track_y_start);
    lv_obj_move_foreground(scroll_bar);

    strncpy(current_path, "/assets", sizeof(current_path));
    selected = 0;
    build_file_list();

    if (nav_timer == NULL)
        nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);

    lv_screen_load(screen_files);
}
