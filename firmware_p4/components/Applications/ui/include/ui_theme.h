#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"
#include <stdint.h>

typedef struct {
    lv_color_t bg_primary;
    lv_color_t bg_secondary;
    lv_color_t bg_item_top;
    lv_color_t bg_item_bot;
    lv_color_t border_accent;
    lv_color_t border_interface;
    lv_color_t border_inactive;
    lv_color_t text_main;
    lv_color_t screen_base;
} ui_theme_t;

extern ui_theme_t current_theme;

void ui_theme_init(void);
void ui_theme_load_idx(int color_idx);

#endif