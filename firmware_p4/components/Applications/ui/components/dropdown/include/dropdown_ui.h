#ifndef UI_DROPDOWN_H
#define UI_DROPDOWN_H

#include "lvgl.h"

void dropdown_ui_create(lv_obj_t * parent);
void dropdown_ui_register_hide_objs(lv_obj_t ** objs, int count);
bool dropdown_ui_is_open(void);
void dropdown_ui_raise(void);

#endif
