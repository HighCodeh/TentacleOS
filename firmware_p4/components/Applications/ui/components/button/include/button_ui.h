#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include "lvgl.h"

typedef struct {
    lv_obj_t * obj;
    lv_obj_t * dot;
    lv_obj_t * icon;
    lv_obj_t * label;
} button_ui_t;

/* Create button with optional dot color and icon */
button_ui_t button_ui_create(lv_obj_t * parent,
                             lv_coord_t width,
                             lv_coord_t height,
                             const char * text,
                             const char * icon_path,
                             const lv_color_t * dot_color);

/* Visual states */
void button_ui_set_selected(button_ui_t * btn, bool selected);
void button_ui_set_dim(button_ui_t * btn, bool dim);
void button_ui_set_text(button_ui_t * btn, const char * text);

#endif
