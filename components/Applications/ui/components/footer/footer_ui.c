#include "footer_ui.h"

#define FOOTER_HEIGHT 20

/* Colors (HEX / LVGL) */
#define FOOTER_BG_COLOR     0x1A053D
#define FOOTER_BORDER_COLOR 0x5E12A0

void footer_ui_create(lv_obj_t * parent)
{
    lv_obj_t * footer = lv_obj_create(parent);
    lv_obj_set_size(footer, lv_pct(100), FOOTER_HEIGHT);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    /* Background */
    lv_obj_set_style_bg_color(
        footer,
        lv_color_hex(FOOTER_BG_COLOR),
        0
    );

    /* Top border */
    lv_obj_set_style_border_side(
        footer,
        LV_BORDER_SIDE_TOP,
        0
    );
    lv_obj_set_style_border_width(footer, 2, 0);
    lv_obj_set_style_border_color(
        footer,
        lv_color_hex(FOOTER_BORDER_COLOR),
        0
    );

    lv_obj_set_style_radius(footer, 0, 0);
}
