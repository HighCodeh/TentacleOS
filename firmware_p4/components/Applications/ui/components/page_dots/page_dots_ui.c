#include "page_dots_ui.h"
#include "ui_theme.h"

static const int DOT_PATTERN[] = {4, 7, 12, 7, 4};
#define PATTERN_LEN 5
#define ANIM_MS 250

page_dots_t page_dots_create(lv_obj_t * parent, int total, lv_align_t align, int x_ofs, int y_ofs)
{
    page_dots_t pd = {0};
    pd.total = (total > PAGE_DOTS_MAX) ? PAGE_DOTS_MAX : total;
    pd.current = 0;

    pd.container = lv_obj_create(parent);
    lv_obj_set_size(pd.container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(pd.container, align, x_ofs, y_ofs);
    lv_obj_set_flex_flow(pd.container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pd.container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(pd.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pd.container, 0, 0);
    lv_obj_set_style_pad_all(pd.container, 0, 0);
    lv_obj_set_style_pad_column(pd.container, 6, 0);
    lv_obj_remove_flag(pd.container, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < pd.total; i++) {
        pd.dots[i] = lv_obj_create(pd.container);
        lv_obj_set_size(pd.dots[i], 4, 4);
        lv_obj_set_style_radius(pd.dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(pd.dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pd.dots[i], current_theme.text_main, 0);
        lv_obj_set_style_border_width(pd.dots[i], 0, 0);
        lv_obj_remove_flag(pd.dots[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    page_dots_set(&pd, 0);

    return pd;
}

void page_dots_set(page_dots_t * pd, int index)
{
    if (!pd || index < 0 || index >= pd->total) return;
    pd->current = index;

    for (int i = 0; i < pd->total; i++) {
        int rel = i - index;
        int dist = rel < 0 ? -rel : rel;

        if (dist <= 2) {
            lv_obj_remove_flag(pd->dots[i], LV_OBJ_FLAG_HIDDEN);
            int sz = DOT_PATTERN[2 + rel];
            lv_obj_set_size(pd->dots[i], sz, sz);
        } else {
            lv_obj_add_flag(pd->dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void page_dots_show(page_dots_t * pd)
{
    if (pd && pd->container) lv_obj_remove_flag(pd->container, LV_OBJ_FLAG_HIDDEN);
}

void page_dots_hide(page_dots_t * pd)
{
    if (pd && pd->container) lv_obj_add_flag(pd->container, LV_OBJ_FLAG_HIDDEN);
}
