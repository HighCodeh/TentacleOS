#include "button_ui.h"
#include "ui_theme.h"

void button_ui_apply_style(lv_obj_t * btn)
{
    if (!btn) return;
    lv_obj_set_style_bg_color(btn, current_theme.bg_item_bot, 0);
    lv_obj_set_style_bg_grad_color(btn, current_theme.bg_item_top, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, current_theme.border_inactive, 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_text_color(btn, current_theme.text_main, 0);
    lv_obj_set_style_border_color(btn, current_theme.border_accent, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUS_KEY);
}

lv_obj_t * button_ui_create(lv_obj_t * parent,
                            lv_coord_t width,
                            lv_coord_t height,
                            const char * text)
{
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, height);
    button_ui_apply_style(btn);

    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_center(lbl);

    return btn;
}
