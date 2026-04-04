#include "toggle_ui.h"
#include "ui_theme.h"

#define DOT_SIZE        19
#define TOGGLE_W        52
#define TOGGLE_H        24

#define COLOR_OFF_TOP   current_theme.bg_item_top
#define COLOR_OFF_BOT   current_theme.bg_secondary
#define COLOR_ON_TOP    current_theme.border_accent
#define COLOR_ON_BOT    current_theme.border_accent

static void apply_style(toggle_ui_t * toggle)
{
    bool on = toggle->state;

    if (on) {
        lv_obj_set_style_bg_color(toggle->obj, COLOR_ON_TOP, 0);
        lv_obj_set_style_bg_grad_color(toggle->obj, COLOR_ON_BOT, 0);
        lv_obj_set_style_bg_color(toggle->dot, COLOR_OFF_TOP, 0);
        lv_obj_set_style_bg_grad_color(toggle->dot, COLOR_OFF_BOT, 0);
    } else {
        lv_obj_set_style_bg_color(toggle->obj, COLOR_OFF_TOP, 0);
        lv_obj_set_style_bg_grad_color(toggle->obj, COLOR_OFF_BOT, 0);
        lv_obj_set_style_bg_color(toggle->dot, COLOR_ON_TOP, 0);
        lv_obj_set_style_bg_grad_color(toggle->dot, COLOR_ON_BOT, 0);
    }
}

void toggle_ui_create(toggle_ui_t * toggle, lv_obj_t * parent)
{
    toggle->state = false;

    toggle->obj = lv_obj_create(parent);
    lv_obj_set_size(toggle->obj, TOGGLE_W, TOGGLE_H);
    lv_obj_remove_flag(toggle->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(toggle->obj, 12, 0);
    lv_obj_set_style_bg_opa(toggle->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(toggle->obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(toggle->obj, 0, 0);
    lv_obj_set_style_pad_left(toggle->obj, 3, 0);
    lv_obj_set_style_pad_right(toggle->obj, 3, 0);

    toggle->dot = lv_obj_create(toggle->obj);
    lv_obj_set_size(toggle->dot, DOT_SIZE, DOT_SIZE);
    lv_obj_remove_flag(toggle->dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(toggle->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(toggle->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(toggle->dot, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(toggle->dot, 0, 0);
    lv_obj_align(toggle->dot, LV_ALIGN_LEFT_MID, 0, 0);

    apply_style(toggle);
}

void toggle_ui_set(toggle_ui_t * toggle, bool on)
{
    if (toggle->state == on) return;
    toggle->state = on;
    apply_style(toggle);

    if (on) {
        lv_obj_align(toggle->dot, LV_ALIGN_RIGHT_MID, 0, 0);
    } else {
        lv_obj_align(toggle->dot, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

void toggle_ui_toggle(toggle_ui_t * toggle)
{
    toggle_ui_set(toggle, !toggle->state);
}

bool toggle_ui_get(toggle_ui_t * toggle)
{
    return toggle->state;
}
