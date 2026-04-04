#ifndef UI_PAGE_DOTS_H
#define UI_PAGE_DOTS_H

#include "lvgl.h"

#define PAGE_DOTS_MAX 16

typedef struct {
    lv_obj_t * container;
    lv_obj_t * dots[PAGE_DOTS_MAX];
    int total;
    int current;
} page_dots_t;

/* Create page dots indicator. Shows 5 dots centered on current page: [4][7][12][7][4] */
page_dots_t page_dots_create(lv_obj_t * parent, int total, lv_align_t align, int x_ofs, int y_ofs);

/* Update which page is selected — animates dot sizes */
void page_dots_set(page_dots_t * pd, int index);

/* Show/hide the dots */
void page_dots_show(page_dots_t * pd);
void page_dots_hide(page_dots_t * pd);

#endif
