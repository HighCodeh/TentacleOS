#include "dropdown_ui.h"
#include "ui_theme.h"
#include "toggle_ui.h"
#include "page_dots_ui.h"
#include "assets_manager.h"
#include "buttons_gpio.h"
#include "st7789.h"

#define DROPDOWN_HEIGHT_P0 ((LCD_V_RES * 85) / 100)
#define DROPDOWN_HEIGHT_P1 ((LCD_V_RES * 50) / 100)
#define DROPDOWN_HEIGHT    DROPDOWN_HEIGHT_P0
#define SEL_ITEMS       5
#define BORDER_SEL_COLOR current_theme.border_accent

static const int page_heights[] = {DROPDOWN_HEIGHT_P0, DROPDOWN_HEIGHT_P1};

static lv_obj_t * slide_panel = NULL;
static lv_obj_t * slide_bar_obj = NULL;
static page_dots_t pg_dots;
static int current_page = 0;
#define DROPDOWN_PAGES 2
static lv_obj_t * page_containers[DROPDOWN_PAGES] = {NULL};
static bool slide_open = false;
static bool slide_animating = false;

static lv_obj_t ** hide_objs_ref = NULL;
static int hide_objs_count = 0;

static lv_obj_t * sel_items[SEL_ITEMS] = {NULL};
static int selected_idx = 0;

static toggle_ui_t toggles[2];
static lv_obj_t * circles[2] = {NULL};
static lv_obj_t * circle_icons_obj[2] = {NULL};

#define SLIDER_STEPS    10
#define SLIDER_MAX_W    100
static lv_obj_t * slider_bars[3] = {NULL};
static int slider_vals[3] = {5, 5, 5};

static bool btn_up_last = false;
static bool btn_down_last = false;
static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_ok_last = false;
static bool btn_back_last = false;
static lv_timer_t * slide_btn_timer = NULL;

static void update_slider(int idx)
{
    if (idx < 0 || idx >= 3 || !slider_bars[idx]) return;
    int32_t pct = (100 * slider_vals[idx]) / SLIDER_STEPS;
    if (pct < 1) pct = 1;
    lv_obj_set_size(slider_bars[idx], lv_pct(pct), 33);
}

static void update_selection(void)
{
    for (int i = 0; i < SEL_ITEMS; i++) {
        if (!sel_items[i]) continue;
        if (i == selected_idx) {
            lv_obj_set_style_border_width(sel_items[i], 2, 0);
            lv_obj_set_style_border_color(sel_items[i], BORDER_SEL_COLOR, 0);
        } else {
            lv_obj_set_style_border_width(sel_items[i], 0, 0);
        }
    }
}

static void update_circle(int idx)
{
    bool on = toggle_ui_get(&toggles[idx]);

    if (circles[idx]) {
        if (on) {
            lv_obj_set_style_bg_color(circles[idx], current_theme.border_accent, 0);
            lv_obj_set_style_bg_grad_color(circles[idx], current_theme.border_accent, 0);
        } else {
            lv_obj_set_style_bg_color(circles[idx], current_theme.bg_item_top, 0);
            lv_obj_set_style_bg_grad_color(circles[idx], current_theme.bg_secondary, 0);
        }
    }

    if (circle_icons_obj[idx]) {
        if (on) {
            lv_obj_set_style_image_recolor(circle_icons_obj[idx], current_theme.screen_base, 0);
            lv_obj_set_style_image_recolor_opa(circle_icons_obj[idx], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_image_recolor_opa(circle_icons_obj[idx], LV_OPA_TRANSP, 0);
        }
    }
}

static void animate_to_page_height(int page) {
    int h = page_heights[page];
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, slide_panel);
    lv_anim_set_values(&a, lv_obj_get_height(slide_panel), h);
    lv_anim_set_duration(&a, 250);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_height);
    lv_anim_start(&a);

    if (slide_bar_obj) {
        lv_anim_set_var(&a, slide_bar_obj);
        lv_anim_set_values(&a, lv_obj_get_y(slide_bar_obj), h - 6);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_start(&a);
    }
}

static void slide_anim_cb(void * var, int32_t val)
{
    lv_obj_set_y((lv_obj_t *)var, val);
    if (slide_bar_obj) {
        lv_obj_set_y(slide_bar_obj, val + DROPDOWN_HEIGHT - 6);
    }
}

static void slide_anim_done_cb(lv_anim_t * a)
{
    slide_animating = false;
    if (!slide_open) {
        page_dots_hide(&pg_dots);
        if (slide_bar_obj) lv_obj_add_flag(slide_bar_obj, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < hide_objs_count; i++) {
            if (hide_objs_ref[i]) lv_obj_remove_flag(hide_objs_ref[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void dropdown_open(void)
{
    if (!slide_panel || slide_animating || slide_open) return;
    slide_animating = true;

    selected_idx = 0;
    update_selection();

    for (int p = 0; p < DROPDOWN_PAGES; p++) {
        if (page_containers[p]) {
            if (p == 0) lv_obj_remove_flag(page_containers[p], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(page_containers[p], LV_OBJ_FLAG_HIDDEN);
        }
    }
    current_page = 0;
    lv_obj_set_height(slide_panel, page_heights[0]);

    lv_obj_remove_flag(slide_panel, LV_OBJ_FLAG_HIDDEN);
    if (slide_bar_obj) lv_obj_remove_flag(slide_bar_obj, LV_OBJ_FLAG_HIDDEN);
    page_dots_show(&pg_dots);
    page_dots_set(&pg_dots, 0);
    for (int i = 0; i < hide_objs_count; i++) {
        if (hide_objs_ref[i]) lv_obj_add_flag(hide_objs_ref[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, slide_panel);
    lv_anim_set_exec_cb(&a, slide_anim_cb);
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, slide_anim_done_cb);
    lv_anim_set_values(&a, -DROPDOWN_HEIGHT, 0);
    lv_anim_start(&a);
    slide_open = true;
}

static void dropdown_close(void)
{
    if (!slide_panel || slide_animating || !slide_open) return;
    slide_animating = true;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, slide_panel);
    lv_anim_set_exec_cb(&a, slide_anim_cb);
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, slide_anim_done_cb);
    lv_anim_set_values(&a, 0, -DROPDOWN_HEIGHT);
    lv_anim_start(&a);
    slide_open = false;
}

static void slide_btn_timer_cb(lv_timer_t * timer)
{
    if (!slide_panel || !lv_obj_is_valid(slide_panel)) {
        lv_timer_delete(timer);
        slide_btn_timer = NULL;
        return;
    }
    bool up_pressed    = up_button_is_down();
    bool down_pressed  = down_button_is_down();
    bool left_pressed  = left_button_is_down();
    bool right_pressed = right_button_is_down();
    bool ok_pressed    = ok_button_is_down();
    bool back_pressed  = back_button_is_down();

    if (up_pressed && !btn_up_last) {
        if (!slide_open) {
            dropdown_open();
        } else if (selected_idx > 0) {
            selected_idx--;
            update_selection();
        }
    }

    if (down_pressed && !btn_down_last && slide_open) {
        if (selected_idx < SEL_ITEMS - 1) {
            selected_idx++;
            update_selection();
        }
    }

    if (left_pressed && !btn_left_last && slide_open) {
        if (current_page > 0) {
            if (page_containers[current_page]) lv_obj_add_flag(page_containers[current_page], LV_OBJ_FLAG_HIDDEN);
            current_page--;
            if (page_containers[current_page]) lv_obj_remove_flag(page_containers[current_page], LV_OBJ_FLAG_HIDDEN);
            page_dots_set(&pg_dots, current_page);
            animate_to_page_height(current_page);
            selected_idx = 0;
            update_selection();
        }
    }
    if (right_pressed && !btn_right_last && slide_open) {
        if (current_page < DROPDOWN_PAGES - 1) {
            if (page_containers[current_page]) lv_obj_add_flag(page_containers[current_page], LV_OBJ_FLAG_HIDDEN);
            current_page++;
            if (page_containers[current_page]) lv_obj_remove_flag(page_containers[current_page], LV_OBJ_FLAG_HIDDEN);
            page_dots_set(&pg_dots, current_page);
            animate_to_page_height(current_page);
            selected_idx = 0;
            update_selection();
        }
    }

    if (ok_pressed && !btn_ok_last && slide_open) {
        if (selected_idx < 2) {
            toggle_ui_toggle(&toggles[selected_idx]);
            update_circle(selected_idx);
        }
    }

    if (back_pressed && !btn_back_last && slide_open) {
        dropdown_close();
    }

    btn_up_last    = up_pressed;
    btn_down_last  = down_pressed;
    btn_left_last  = left_pressed;
    btn_right_last = right_pressed;
    btn_ok_last    = ok_pressed;
    btn_back_last  = back_pressed;
}

void dropdown_ui_create(lv_obj_t * parent)
{
    slide_panel = lv_obj_create(parent);
    lv_obj_set_size(slide_panel, lv_pct(100), DROPDOWN_HEIGHT);
    lv_obj_set_pos(slide_panel, 0, -DROPDOWN_HEIGHT);
    lv_obj_remove_flag(slide_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(slide_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_move_foreground(slide_panel);

    lv_obj_set_style_radius(slide_panel, 12, 0);
    lv_obj_set_style_border_side(slide_panel, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(slide_panel, 2, 0);
    lv_obj_set_style_border_color(slide_panel, current_theme.border_accent, 0);
    lv_obj_set_style_pad_all(slide_panel, 0, 0);
    lv_obj_set_style_bg_opa(slide_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(slide_panel, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_color(slide_panel, current_theme.border_interface, 0);
    lv_obj_set_style_bg_grad_dir(slide_panel, LV_GRAD_DIR_VER, 0);

    page_containers[0] = lv_obj_create(slide_panel);
    lv_obj_t * content = page_containers[0];
    lv_obj_set_size(content, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 10, 0);

    lv_obj_t * row_circles = lv_obj_create(content);
    lv_obj_set_size(row_circles, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(row_circles, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_circles, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_circles, 0, 0);
    lv_obj_set_style_pad_all(row_circles, 0, 0);
    lv_obj_set_style_pad_column(row_circles, 20, 0);
    lv_obj_set_flex_flow(row_circles, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_circles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static lv_image_dsc_t * bt_sel_dsc = NULL;
    static lv_image_dsc_t * wifi_sel_dsc = NULL;
    if (!bt_sel_dsc) bt_sel_dsc = assets_get("/assets/icons/bluetooth_sel.bin");
    if (!wifi_sel_dsc) wifi_sel_dsc = assets_get("/assets/icons/wifi_sel.bin");
    lv_image_dsc_t * circle_icon_dscs[] = {bt_sel_dsc, wifi_sel_dsc};

    for (int i = 0; i < 2; i++) {
        lv_obj_t * circle = lv_obj_create(row_circles);
        lv_obj_set_size(circle, 67, 67);
        lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(circle, current_theme.bg_item_top, 0);
        lv_obj_set_style_bg_grad_color(circle, current_theme.bg_secondary, 0);
        lv_obj_set_style_bg_grad_dir(circle, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(circle, 0, 0);
        circles[i] = circle;

        if (circle_icon_dscs[i]) {
            lv_obj_t * icon = lv_image_create(circle);
            lv_image_set_src(icon, circle_icon_dscs[i]);
            lv_obj_center(icon);
            circle_icons_obj[i] = icon;
        }
    }

    lv_obj_t * row_toggles = lv_obj_create(content);
    lv_obj_set_size(row_toggles, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(row_toggles, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_toggles, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_toggles, 0, 0);
    lv_obj_set_style_pad_all(row_toggles, 0, 0);
    lv_obj_set_style_pad_column(row_toggles, 20, 0);
    lv_obj_set_flex_flow(row_toggles, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_toggles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 2; i++) {
        toggle_ui_create(&toggles[i], row_toggles);
        sel_items[i] = toggles[i].obj;
    }

    static lv_image_dsc_t * phone_dsc = NULL;
    static lv_image_dsc_t * volume_dsc = NULL;
    static lv_image_dsc_t * bright_dsc = NULL;

    if (!phone_dsc) phone_dsc = assets_get("/assets/icons/phone_icon.bin");
    if (!volume_dsc) volume_dsc = assets_get("/assets/icons/volume_icon.bin");
    if (!bright_dsc) bright_dsc = assets_get("/assets/icons/bright_icon.bin");

    lv_image_dsc_t * big_icons[] = {phone_dsc, volume_dsc, bright_dsc};

    for (int i = 0; i < 3; i++) {
        lv_obj_t * big_rect = lv_obj_create(content);
        lv_obj_set_size(big_rect, lv_pct(80), 33);
        lv_obj_remove_flag(big_rect, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(big_rect, 12, 0);
        lv_obj_set_style_bg_opa(big_rect, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(big_rect, current_theme.bg_item_top, 0);
        lv_obj_set_style_bg_grad_color(big_rect, current_theme.bg_secondary, 0);
        lv_obj_set_style_bg_grad_dir(big_rect, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_width(big_rect, 0, 0);
        lv_obj_set_style_pad_all(big_rect, 0, 0);

        int32_t pct = (100 * slider_vals[i]) / SLIDER_STEPS;
        if (pct < 1) pct = 1;
        lv_obj_t * bar = lv_obj_create(big_rect);
        lv_obj_set_size(bar, lv_pct(pct), 33);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(bar, 12, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, current_theme.border_accent, 0);
        lv_obj_set_style_bg_grad_color(bar, current_theme.border_accent, 0);
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_set_pos(bar, 0, 0);
        slider_bars[i] = bar;

        if (big_icons[i]) {
            lv_obj_t * icon = lv_image_create(big_rect);
            lv_image_set_src(icon, big_icons[i]);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 8, 0);
        }

        sel_items[2 + i] = big_rect;
    }

    static lv_image_dsc_t * slide_bar_dsc = NULL;
    if (!slide_bar_dsc) slide_bar_dsc = assets_get("/assets/icons/slide_bar.bin");

    slide_bar_obj = lv_image_create(parent);
    if (slide_bar_dsc) lv_image_set_src(slide_bar_obj, slide_bar_dsc);
    lv_obj_align(slide_bar_obj, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_y(slide_bar_obj, -DROPDOWN_HEIGHT);
    lv_obj_add_flag(slide_bar_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(slide_bar_obj);

    page_containers[1] = lv_obj_create(slide_panel);
    lv_obj_set_size(page_containers[1], lv_pct(95), LV_SIZE_CONTENT);
    lv_obj_align(page_containers[1], LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_remove_flag(page_containers[1], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page_containers[1], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page_containers[1], 0, 0);
    lv_obj_set_style_pad_all(page_containers[1], 0, 0);
    lv_obj_set_flex_flow(page_containers[1], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page_containers[1], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(page_containers[1], 10, 0);
    lv_obj_add_flag(page_containers[1], LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * avatar = lv_obj_create(page_containers[1]);
    lv_obj_set_size(avatar, 80, 80);
    lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(avatar, current_theme.screen_base, 0);
    lv_obj_set_style_border_width(avatar, 2, 0);
    lv_obj_set_style_border_color(avatar, current_theme.border_accent, 0);
    lv_obj_set_style_pad_all(avatar, 0, 0);

    static lv_image_dsc_t * portrait_dsc = NULL;
    if (!portrait_dsc) portrait_dsc = assets_get("/assets/img/octobit_portrait.bin");
    if (portrait_dsc) {
        lv_obj_t * portrait = lv_image_create(avatar);
        lv_image_set_src(portrait, portrait_dsc);
        lv_obj_center(portrait);
    }

    lv_obj_t * tag = lv_obj_create(avatar);
    lv_obj_set_size(tag, 45, 15);
    lv_obj_remove_flag(tag, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(tag, 7, 0);
    lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tag, current_theme.border_accent, 0);
    lv_obj_set_style_border_width(tag, 0, 0);
    lv_obj_set_style_pad_all(tag, 0, 0);
    lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, 2, -2);
    lv_obj_move_foreground(tag);

    lv_obj_t * tag_lbl = lv_label_create(tag);
    lv_label_set_text(tag_lbl, "octo");
    lv_obj_set_style_text_color(tag_lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(tag_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(tag_lbl);

    lv_obj_t * bars_col = lv_obj_create(page_containers[1]);
    lv_obj_set_size(bars_col, 120, LV_SIZE_CONTENT);
    lv_obj_remove_flag(bars_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(bars_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bars_col, 0, 0);
    lv_obj_set_style_pad_all(bars_col, 0, 0);
    lv_obj_set_style_pad_row(bars_col, 6, 0);
    lv_obj_set_flex_flow(bars_col, LV_FLEX_FLOW_COLUMN);

    static const int bar_pcts[] = {80, 55, 40, 65};
    for (int i = 0; i < 4; i++) {
        lv_obj_t * bar_bg = lv_obj_create(bars_col);
        lv_obj_set_size(bar_bg, lv_pct(100), 11);
        lv_obj_remove_flag(bar_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(bar_bg, 5, 0);
        lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar_bg, current_theme.bg_item_top, 0);
        lv_obj_set_style_border_width(bar_bg, 0, 0);
        lv_obj_set_style_pad_all(bar_bg, 0, 0);

        lv_obj_t * bar_fill = lv_obj_create(bar_bg);
        lv_obj_set_size(bar_fill, lv_pct(bar_pcts[i]), 11);
        lv_obj_remove_flag(bar_fill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(bar_fill, 5, 0);
        lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar_fill, current_theme.border_accent, 0);
        lv_obj_set_style_bg_grad_color(bar_fill, current_theme.border_accent, 0);
        lv_obj_set_style_bg_grad_dir(bar_fill, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_border_width(bar_fill, 0, 0);
        lv_obj_set_style_pad_all(bar_fill, 0, 0);
        lv_obj_set_pos(bar_fill, 0, 0);
    }

    int dots_y = -(LCD_V_RES - DROPDOWN_HEIGHT - 20) / 2 - 5;
    pg_dots = page_dots_create(parent, DROPDOWN_PAGES, LV_ALIGN_BOTTOM_MID, 0, dots_y);
    page_dots_hide(&pg_dots);
    lv_obj_move_foreground(pg_dots.container);

    if (slide_btn_timer == NULL) {
        slide_btn_timer = lv_timer_create(slide_btn_timer_cb, 50, NULL);
    }

    slide_open = false;
    slide_animating = false;
}

void dropdown_ui_register_hide_objs(lv_obj_t ** objs, int count)
{
    hide_objs_ref = objs;
    hide_objs_count = count;
}

bool dropdown_ui_is_open(void)
{
    return slide_open;
}

void dropdown_ui_raise(void)
{
    if (slide_panel) lv_obj_move_foreground(slide_panel);
    if (slide_bar_obj) lv_obj_move_foreground(slide_bar_obj);
    if (pg_dots.container) lv_obj_move_foreground(pg_dots.container);
}
