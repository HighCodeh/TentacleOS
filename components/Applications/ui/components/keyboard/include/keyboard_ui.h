#ifndef KEYBOARD_UI_H
#define KEYBOARD_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*keyboard_submit_cb_t)(const char * text, void * user_data);

void keyboard_open(lv_obj_t * target_textarea, keyboard_submit_cb_t cb, void * user_data);
void keyboard_close(void);

#ifdef __cplusplus
}
#endif

#endif /*KEYBOARD_UI_H*/
