#include "menu_component_ui.h"
#include "ui_theme.h"

static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static void menu_component_init_styles(void) {
    if (styles_initialized) return;

    lv_style_init(&style_menu);
    lv_style_set_bg_opa(&style_menu, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_menu, 2);
    lv_style_set_border_color(&style_menu, current_theme.border_accent);
    lv_style_set_radius(&style_menu, 0);
    lv_style_set_pad_all(&style_menu, 8);
    lv_style_set_pad_row(&style_menu, 8);

    lv_style_init(&style_item);
    lv_style_set_bg_color(&style_item, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_item, current_theme.bg_item_top);
    lv_style_set_bg_grad_dir(&style_item, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_item, 1);
    lv_style_set_border_color(&style_item, current_theme.border_inactive);
    lv_style_set_radius(&style_item, 0);

    styles_initialized = true;
}

menu_component_t menu_component_create(lv_obj_t * parent,
                                       lv_coord_t width,
                                       lv_coord_t height,
                                       lv_align_t align,
                                       lv_coord_t x_ofs,
                                       lv_coord_t y_ofs)
{
    menu_component_t menu = {0};
    menu_component_init_styles();

    menu.container = lv_obj_create(parent);
    lv_obj_set_size(menu.container, width, height);
    lv_obj_align(menu.container, align, x_ofs, y_ofs);
    lv_obj_add_style(menu.container, &style_menu, 0);
    lv_obj_set_flex_flow(menu.container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(menu.container, LV_SCROLLBAR_MODE_OFF);

    return menu;
}

lv_obj_t * menu_component_add_item(menu_component_t * menu,
                                   const char * symbol,
                                   const char * label)
{
    if (!menu || !menu->container) return NULL;

    lv_obj_t * item = lv_obj_create(menu->container);
    lv_obj_set_size(item, lv_pct(100), 40);
    lv_obj_add_style(item, &style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * icon = lv_label_create(item);
    lv_label_set_text(icon, symbol ? symbol : "");
    lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

    lv_obj_t * lbl = lv_label_create(item);
    lv_label_set_text(lbl, label ? label : "");
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_margin_left(lbl, 8, 0);

    return item;
}

lv_obj_t * menu_component_add_nav_item(menu_component_t * menu,
                                       const char * symbol,
                                       const char * label,
                                       lv_obj_t ** out_arrow_label)
{
    lv_obj_t * item = menu_component_add_item(menu, symbol, label);
    if (!item) return NULL;

    lv_obj_t * arrow = lv_label_create(item);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(arrow, current_theme.text_main, 0);

    if (out_arrow_label) *out_arrow_label = arrow;
    return item;
}

lv_obj_t * menu_component_add_value_item(menu_component_t * menu,
                                         const char * symbol,
                                         const char * label,
                                         const char * value,
                                         lv_obj_t ** out_value_label)
{
    lv_obj_t * item = menu_component_add_item(menu, symbol, label);
    if (!item) return NULL;

    lv_obj_t * val = lv_label_create(item);
    lv_label_set_text(val, value ? value : "");
    lv_obj_set_style_text_color(val, current_theme.text_main, 0);

    if (out_value_label) *out_value_label = val;
    return item;
}

lv_obj_t * menu_component_add_switch_item(menu_component_t * menu,
                                          const char * symbol,
                                          const char * label,
                                          lv_obj_t ** out_switch_container)
{
    lv_obj_t * item = menu_component_add_item(menu, symbol, label);
    if (!item) return NULL;

    lv_obj_t * sw_cont = lv_obj_create(item);
    lv_obj_set_size(sw_cont, 60, 32);
    lv_obj_set_style_bg_opa(sw_cont, 0, 0);
    lv_obj_set_style_border_width(sw_cont, 0, 0);
    lv_obj_set_flex_flow(sw_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sw_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(sw_cont, 0, 0);
    lv_obj_set_style_pad_gap(sw_cont, 4, 0);
    lv_obj_clear_flag(sw_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sw_cont, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < 2; i++) {
        lv_obj_t * b = lv_obj_create(sw_cont);
        lv_obj_set_size(b, 24, 26);
        lv_obj_set_style_bg_color(b, current_theme.text_main, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    }

    if (out_switch_container) *out_switch_container = sw_cont;
    return item;
}
