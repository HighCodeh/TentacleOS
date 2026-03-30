#ifndef UI_TOGGLE_H
#define UI_TOGGLE_H

#include "lvgl.h"
#include <stdbool.h>

typedef struct {
    lv_obj_t * obj;
    lv_obj_t * dot;
    bool state;
} toggle_ui_t;

void toggle_ui_create(toggle_ui_t * toggle, lv_obj_t * parent);
void toggle_ui_set(toggle_ui_t * toggle, bool on);
void toggle_ui_toggle(toggle_ui_t * toggle);
bool toggle_ui_get(toggle_ui_t * toggle);

#endif
