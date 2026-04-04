#include "intensity_bar_ui.h"
#include "ui_theme.h"

#define BAR_W       8
#define BAR_H       20
#define BAR_GAP     4
#define BAR_RADIUS  3
#define CONT_W      ((BAR_W + BAR_GAP) * INTENSITY_BAR_STEPS + BAR_GAP)
#define CONT_H      (BAR_H + 8)

#define COLOR_ON_L  current_theme.border_accent
#define COLOR_ON_R  current_theme.border_accent
#define COLOR_OFF   current_theme.border_inactive

void intensity_bar_create(intensity_bar_t * ib, lv_obj_t * parent)
{
    ib->level = 0;

    ib->obj = lv_obj_create(parent);
    lv_obj_set_size(ib->obj, CONT_W, CONT_H);
    lv_obj_remove_flag(ib->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ib->obj, 8, 0);
    lv_obj_set_style_bg_opa(ib->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ib->obj, current_theme.bg_item_top, 0);
    lv_obj_set_style_border_width(ib->obj, 1, 0);
    lv_obj_set_style_border_color(ib->obj, current_theme.border_accent, 0);
    lv_obj_set_style_pad_all(ib->obj, 3, 0);
    lv_obj_set_flex_flow(ib->obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ib->obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ib->obj, BAR_GAP, 0);

    for (int i = 0; i < INTENSITY_BAR_STEPS; i++) {
        ib->bars[i] = lv_obj_create(ib->obj);
        lv_obj_set_size(ib->bars[i], BAR_W, BAR_H);
        lv_obj_remove_flag(ib->bars[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(ib->bars[i], BAR_RADIUS, 0);
        lv_obj_set_style_bg_opa(ib->bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(ib->bars[i], COLOR_OFF, 0);
        lv_obj_set_style_border_width(ib->bars[i], 0, 0);
    }
}

void intensity_bar_set(intensity_bar_t * ib, int level)
{
    if (!ib) return;
    if (level < 0) level = 0;
    if (level > INTENSITY_BAR_STEPS) level = INTENSITY_BAR_STEPS;
    ib->level = level;

    for (int i = 0; i < INTENSITY_BAR_STEPS; i++) {
        if (i < level) {
            lv_obj_set_style_bg_color(ib->bars[i], COLOR_ON_L, 0);
            lv_obj_set_style_bg_grad_color(ib->bars[i], COLOR_ON_R, 0);
            lv_obj_set_style_bg_grad_dir(ib->bars[i], LV_GRAD_DIR_VER, 0);
        } else {
            lv_obj_set_style_bg_color(ib->bars[i], COLOR_OFF, 0);
            lv_obj_set_style_bg_grad_dir(ib->bars[i], LV_GRAD_DIR_NONE, 0);
        }
    }
}

int intensity_bar_get(intensity_bar_t * ib)
{
    return ib ? ib->level : 0;
}

void intensity_bar_inc(intensity_bar_t * ib)
{
    if (ib && ib->level < INTENSITY_BAR_STEPS)
        intensity_bar_set(ib, ib->level + 1);
}

void intensity_bar_dec(intensity_bar_t * ib)
{
    if (ib && ib->level > 1)
        intensity_bar_set(ib, ib->level - 1);
}
