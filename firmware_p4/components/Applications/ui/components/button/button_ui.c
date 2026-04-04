#include "button_ui.h"
#include "assets_manager.h"
#include "ui_theme.h"

#define BTN_BG          current_theme.bg_primary
#define BTN_GRAD        current_theme.bg_secondary
#define BTN_BORDER      current_theme.border_interface
#define BTN_SEL_BORDER  current_theme.border_accent

button_ui_t button_ui_create(lv_obj_t * parent,
                             lv_coord_t width,
                             lv_coord_t height,
                             const char * text,
                             const char * icon_path,
                             const lv_color_t * dot_color)
{
    button_ui_t b = {0};

    b.obj = lv_obj_create(parent);
    lv_obj_set_size(b.obj, width, height);
    lv_obj_remove_flag(b.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(b.obj, height / 2, 0);
    lv_obj_set_style_bg_opa(b.obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b.obj, BTN_BG, 0);
    lv_obj_set_style_bg_grad_color(b.obj, BTN_GRAD, 0);
    lv_obj_set_style_bg_grad_dir(b.obj, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(b.obj, 1, 0);
    lv_obj_set_style_border_color(b.obj, BTN_BORDER, 0);
    lv_obj_set_style_pad_left(b.obj, 10, 0);
    lv_obj_set_style_pad_right(b.obj, 10, 0);
    lv_obj_set_flex_flow(b.obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b.obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b.obj, 8, 0);

    if (dot_color) {
        b.dot = lv_obj_create(b.obj);
        lv_obj_set_size(b.dot, 12, 12);
        lv_obj_remove_flag(b.dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(b.dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(b.dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b.dot, *dot_color, 0);
        lv_obj_set_style_border_width(b.dot, 0, 0);
    }

    if (icon_path) {
        lv_image_dsc_t * dsc = assets_get(icon_path);
        if (dsc) {
            b.icon = lv_image_create(b.obj);
            lv_image_set_src(b.icon, dsc);
        }
    }

    b.label = lv_label_create(b.obj);
    lv_label_set_text(b.label, text ? text : "");
    lv_obj_set_style_text_color(b.label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(b.label, &lv_font_montserrat_12, 0);

    return b;
}

void button_ui_set_selected(button_ui_t * btn, bool selected)
{
    if (!btn || !btn->obj) return;
    if (selected) {
        lv_obj_set_style_border_width(btn->obj, 2, 0);
        lv_obj_set_style_border_color(btn->obj, BTN_SEL_BORDER, 0);
    } else {
        lv_obj_set_style_border_width(btn->obj, 1, 0);
        lv_obj_set_style_border_color(btn->obj, BTN_BORDER, 0);
    }
}

void button_ui_set_dim(button_ui_t * btn, bool dim)
{
    if (!btn || !btn->obj) return;
    lv_obj_set_style_opa(btn->obj, dim ? LV_OPA_50 : LV_OPA_COVER, 0);
}

void button_ui_set_text(button_ui_t * btn, const char * text)
{
    if (!btn || !btn->label) return;
    lv_label_set_text(btn->label, text ? text : "");
}
