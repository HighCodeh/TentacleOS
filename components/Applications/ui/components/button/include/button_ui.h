#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include "lvgl.h"

lv_obj_t * button_ui_create(lv_obj_t * parent,
                            lv_coord_t width,
                            lv_coord_t height,
                            const char * text);

void button_ui_apply_style(lv_obj_t * btn);

#endif
