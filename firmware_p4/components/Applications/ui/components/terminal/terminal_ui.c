#include "terminal_ui.h"
#include "ui_theme.h"

lv_obj_t * terminal_ui_create(lv_obj_t * parent,
                              lv_coord_t width,
                              lv_coord_t height,
                              lv_align_t align,
                              lv_coord_t x_ofs,
                              lv_coord_t y_ofs)
{
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, width, height);
    lv_obj_align(ta, align, x_ofs, y_ofs);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(ta, current_theme.bg_primary, 0);
    lv_obj_set_style_text_color(ta, current_theme.text_main, 0);
    lv_obj_set_style_radius(ta, 0, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, current_theme.border_accent, 0);
    lv_obj_set_style_border_width(ta, 1, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(ta, current_theme.border_accent, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUS_KEY);
    return ta;
}
