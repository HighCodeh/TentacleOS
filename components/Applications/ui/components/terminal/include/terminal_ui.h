#ifndef UI_TERMINAL_H
#define UI_TERMINAL_H

#include "lvgl.h"

lv_obj_t * terminal_ui_create(lv_obj_t * parent,
                              lv_coord_t width,
                              lv_coord_t height,
                              lv_align_t align,
                              lv_coord_t x_ofs,
                              lv_coord_t y_ofs);

#endif
