#include "menu_component_ui.h"
#include "assets_manager.h"
#include "ui_theme.h"
#include "lvgl.h"
#include "st7789.h"

#define BORDER_COLOR     current_theme.border_interface
#define ITEM_BORDER      current_theme.border_accent
#define GRAD_LEFT        current_theme.bg_primary
#define GRAD_RIGHT       current_theme.bg_secondary
#define SEL_BORDER       current_theme.border_accent
#define SEL_DOT_COLOR    current_theme.border_accent

#define TITLE_W          170
#define TITLE_H          30
#define ITEM_W           210
#define ITEM_H           47
#define OUTER_BORDER     4
#define TOP_BORDER_H     (TITLE_H + 16)
#define SEL_DOT_SIZE     8

static lv_font_t * menu_font = NULL;

static void update_scroll_bar(menu_component_t * m) {
    if (!m->scroll_bar || m->item_count <= 1) return;
    int32_t pos = m->track_y_start +
        (m->selected * (m->track_h - 20)) / (m->item_count - 1);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, m->scroll_bar);
    lv_anim_set_values(&a, lv_obj_get_y(m->scroll_bar), pos);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_start(&a);
}

static void update_selection(menu_component_t * m) {
    for (int i = 0; i < m->item_count; i++) {
        if (i == m->selected) {
            lv_obj_set_style_border_color(m->items[i], SEL_BORDER, 0);
            lv_obj_set_style_border_width(m->items[i], 3, 0);
                    bool has_widget = m->has_toggle[i] || m->has_intensity[i] || m->val_labels[i];
            if (m->sel_dots[i]) {
                if (has_widget) lv_obj_add_flag(m->sel_dots[i], LV_OBJ_FLAG_HIDDEN);
                else lv_obj_remove_flag(m->sel_dots[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_set_style_border_color(m->items[i], ITEM_BORDER, 0);
            lv_obj_set_style_border_width(m->items[i], 1, 0);
            if (m->sel_dots[i]) lv_obj_add_flag(m->sel_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (m->items[m->selected]) {
        lv_obj_scroll_to_view(m->items[m->selected], LV_ANIM_ON);
    }

    update_scroll_bar(m);
}

menu_component_t menu_component_create(lv_obj_t * parent, const char * title, const char * title_icon_path)
{
    menu_component_t m = {0};

    if (!menu_font) {
        menu_font = lv_binfont_create("A:assets/fonts/Inter.bin");
    }

    m.screen = lv_obj_create(parent);
    lv_obj_set_size(m.screen, LCD_H_RES, LCD_V_RES);
    lv_obj_align(m.screen, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(m.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(m.screen, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(m.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(m.screen, 0, 0);

    lv_obj_set_style_border_width(m.screen, OUTER_BORDER, 0);
    lv_obj_set_style_border_color(m.screen, BORDER_COLOR, 0);
    lv_obj_set_style_radius(m.screen, 0, 0);

    lv_obj_t * top_area = lv_obj_create(m.screen);
    lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
    lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_area, 3, 0);
    lv_obj_set_style_border_color(top_area, BORDER_COLOR, 0);
    lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(top_area, 0, 0);
    lv_obj_set_style_pad_all(top_area, 0, 0);


    m.title_bar = lv_obj_create(top_area);
    lv_obj_set_size(m.title_bar, TITLE_W, TITLE_H);
    lv_obj_align(m.title_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(m.title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(m.title_bar, 12, 0);
    lv_obj_set_style_bg_opa(m.title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m.title_bar, GRAD_LEFT, 0);
    lv_obj_set_style_bg_grad_color(m.title_bar, GRAD_RIGHT, 0);
    lv_obj_set_style_bg_grad_dir(m.title_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(m.title_bar, 2, 0);
    lv_obj_set_style_border_color(m.title_bar, ITEM_BORDER, 0);
    lv_obj_set_style_pad_all(m.title_bar, 0, 0);
 
    if (title_icon_path) {
        lv_image_dsc_t * ti_dsc = assets_get(title_icon_path);
        if (ti_dsc) {
            lv_obj_t * ti = lv_image_create(m.title_bar);
            lv_image_set_src(ti, ti_dsc);
            lv_obj_add_flag(ti, LV_OBJ_FLAG_FLOATING);
            lv_obj_align(ti, LV_ALIGN_LEFT_MID, 4, 0);
        }
    }

    m.title_label = lv_label_create(m.title_bar);
    lv_label_set_text(m.title_label, title ? title : "");
    lv_obj_set_style_text_color(m.title_label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(m.title_label, menu_font ? menu_font : &lv_font_montserrat_14, 0);
    lv_obj_center(m.title_label);

    int items_y = TOP_BORDER_H + 4;
    int items_h = LCD_V_RES - items_y - OUTER_BORDER - 4;

    m.items_cont = lv_obj_create(m.screen);
    lv_obj_set_size(m.items_cont, ITEM_W + 8, items_h);
    lv_obj_align(m.items_cont, LV_ALIGN_TOP_LEFT, 4, items_y);
    lv_obj_set_style_bg_opa(m.items_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m.items_cont, 0, 0);
    lv_obj_set_style_pad_all(m.items_cont, 2, 0);
    lv_obj_set_style_pad_row(m.items_cont, 6, 0);
    lv_obj_set_flex_flow(m.items_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(m.items_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(m.items_cont, LV_SCROLL_SNAP_START);

    int track_x = LCD_H_RES - OUTER_BORDER - 9;
    m.track_y_start = items_y + 10;
    m.track_h = items_h - 20;

    static lv_point_precise_t track_pts[2];
    track_pts[0].x = 0; track_pts[0].y = 0;
    track_pts[1].x = 0; track_pts[1].y = m.track_h;

    lv_obj_t * track = lv_line_create(m.screen);
    lv_line_set_points(track, track_pts, 2);
    lv_obj_set_pos(track, track_x, m.track_y_start);
    lv_obj_set_style_line_color(track, current_theme.border_inactive, 0);
    lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_line_width(track, 3, 0);
    lv_obj_set_style_line_dash_width(track, 4, 0);
    lv_obj_set_style_line_dash_gap(track, 4, 0);

    static lv_image_dsc_t * slide_bar_v_dsc = NULL;
    if (!slide_bar_v_dsc) slide_bar_v_dsc = assets_get("/assets/icons/slide_bar_v.bin");

    m.scroll_bar = lv_image_create(m.screen);
    if (slide_bar_v_dsc) lv_image_set_src(m.scroll_bar, slide_bar_v_dsc);
    lv_obj_set_pos(m.scroll_bar, track_x - 4, m.track_y_start);
    lv_obj_move_foreground(m.scroll_bar);

    m.item_count = 0;
    m.selected = 0;

    return m;
}

lv_obj_t * menu_component_add_item(menu_component_t * menu,
                                   const char * icon_path,
                                   const char * label)
{
    if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS) return NULL;

    lv_obj_t * item = lv_obj_create(menu->items_cont);
    lv_obj_set_size(item, ITEM_W, ITEM_H);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(item, 10, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, GRAD_LEFT, 0);
    lv_obj_set_style_bg_grad_color(item, GRAD_RIGHT, 0);
    lv_obj_set_style_bg_grad_dir(item, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_border_color(item, ITEM_BORDER, 0);
    lv_obj_set_style_pad_left(item, 3, 0);
    lv_obj_set_style_pad_right(item, 6, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 2, 0);

    if (icon_path) {
        lv_image_dsc_t * icon_dsc = assets_get(icon_path);
        if (icon_dsc) {
            lv_obj_t * icon = lv_image_create(item);
            lv_image_set_src(icon, icon_dsc);
        }
    }

    lv_obj_t * lbl = lv_label_create(item);
    lv_label_set_text(lbl, label ? label : "");
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_pad_left(lbl, 0, 0);

    static lv_image_dsc_t * pointer_dsc = NULL;
    if (!pointer_dsc) pointer_dsc = assets_get("/assets/icons/pointer.bin");

    lv_obj_t * ptr = lv_image_create(item);
    if (pointer_dsc) lv_image_set_src(ptr, pointer_dsc);
    lv_obj_add_flag(ptr, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ptr, LV_ALIGN_RIGHT_MID, -6, 0);

    int idx = menu->item_count;
    menu->items[idx] = item;
    menu->sel_dots[idx] = ptr;
    menu->val_labels[idx] = NULL;
    menu->has_toggle[idx] = false;
    menu->has_intensity[idx] = false;
    menu->item_count++;

    if (idx == menu->selected) {
        lv_obj_set_style_border_width(item, 3, 0);
        lv_obj_set_style_border_color(item, SEL_BORDER, 0);
        lv_obj_remove_flag(ptr, LV_OBJ_FLAG_HIDDEN);
    }

    return item;
}

lv_obj_t * menu_component_add_selector(menu_component_t * menu,
                                       const char * icon_path,
                                       const char * label,
                                       const char * initial_value)
{
    if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS) return NULL;
    int idx = menu->item_count; /* peek before add_item increments */

    lv_obj_t * item = menu_component_add_item(menu, icon_path, label);
    if (!item) return NULL;

    lv_obj_t * val = lv_label_create(item);
    lv_label_set_text_fmt(val, LV_SYMBOL_LEFT " %s " LV_SYMBOL_RIGHT, initial_value ? initial_value : "");
    lv_obj_set_style_text_color(val, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(val, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -6, 0);

    menu->val_labels[idx] = val;

    return item;
}

void menu_component_set_selector_value(menu_component_t * menu, int index, const char * value) {
    if (!menu || index < 0 || index >= menu->item_count || !menu->val_labels[index]) return;
    lv_label_set_text_fmt(menu->val_labels[index], LV_SYMBOL_LEFT " %s " LV_SYMBOL_RIGHT, value ? value : "");
}

lv_obj_t * menu_component_add_toggle(menu_component_t * menu,
                                     const char * icon_path,
                                     const char * label,
                                     bool initial_state)
{
    if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS) return NULL;
    int idx = menu->item_count;

    lv_obj_t * item = menu_component_add_item(menu, icon_path, label);
    if (!item) return NULL;

    toggle_ui_create(&menu->toggles[idx], item);
    lv_obj_add_flag(menu->toggles[idx].obj, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(menu->toggles[idx].obj, LV_ALIGN_RIGHT_MID, -6, 0);
    toggle_ui_set(&menu->toggles[idx], initial_state);
    menu->has_toggle[idx] = true;

    return item;
}

void menu_component_toggle_item(menu_component_t * menu, int index) {
    if (!menu || index < 0 || index >= menu->item_count || !menu->has_toggle[index]) return;
    toggle_ui_toggle(&menu->toggles[index]);
}

bool menu_component_get_toggle(menu_component_t * menu, int index) {
    if (!menu || index < 0 || index >= menu->item_count || !menu->has_toggle[index]) return false;
    return toggle_ui_get(&menu->toggles[index]);
}

void menu_component_set_toggle(menu_component_t * menu, int index, bool state) {
    if (!menu || index < 0 || index >= menu->item_count || !menu->has_toggle[index]) return;
    toggle_ui_set(&menu->toggles[index], state);
}

lv_obj_t * menu_component_add_intensity(menu_component_t * menu,
                                        const char * icon_path,
                                        const char * label,
                                        int initial_level)
{
    if (!menu || menu->item_count >= MENU_COMP_MAX_ITEMS) return NULL;
    int idx = menu->item_count;

    lv_obj_t * item = menu_component_add_item(menu, icon_path, label);
    if (!item) return NULL;

    intensity_bar_create(&menu->intensities[idx], item);
    lv_obj_add_flag(menu->intensities[idx].obj, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(menu->intensities[idx].obj, LV_ALIGN_RIGHT_MID, -6, 0);
    intensity_bar_set(&menu->intensities[idx], initial_level);
    menu->has_intensity[idx] = true;

    return item;
}

void menu_component_intensity_inc(menu_component_t * menu, int index) {
    if (!menu || index < 0 || index >= menu->item_count || !menu->has_intensity[index]) return;
    intensity_bar_inc(&menu->intensities[index]);
}

void menu_component_intensity_dec(menu_component_t * menu, int index) {
    if (!menu || index < 0 || index >= menu->item_count || !menu->has_intensity[index]) return;
    intensity_bar_dec(&menu->intensities[index]);
}

int menu_component_get_intensity(menu_component_t * menu, int index) {
    if (!menu || index < 0 || index >= menu->item_count || !menu->has_intensity[index]) return 0;
    return intensity_bar_get(&menu->intensities[index]);
}

void menu_component_select(menu_component_t * menu, int index) {
    if (!menu || index < 0 || index >= menu->item_count) return;
    menu->selected = index;
    update_selection(menu);
}

void menu_component_next(menu_component_t * menu) {
    if (!menu || menu->item_count == 0) return;
    menu->selected = (menu->selected + 1) % menu->item_count;
    update_selection(menu);
}

void menu_component_prev(menu_component_t * menu) {
    if (!menu || menu->item_count == 0) return;
    menu->selected = (menu->selected == 0) ? menu->item_count - 1 : menu->selected - 1;
    update_selection(menu);
}

int menu_component_get_selected(menu_component_t * menu) {
    return menu ? menu->selected : -1;
}
