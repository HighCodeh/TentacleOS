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

    lv_color_t protocol_nfc;
    lv_color_t protocol_wifi;
    lv_color_t protocol_ble;
    lv_color_t protocol_subghz;
    lv_color_t protocol_rfid;
    lv_color_t protocol_ir;
    lv_color_t protocol_lora;
} ui_theme_t;

typedef enum {
    PROTOCOL_NONE,
    PROTOCOL_NFC,
    PROTOCOL_WIFI,
    PROTOCOL_BLE,
    PROTOCOL_SUBGHZ,
    PROTOCOL_RFID,
    PROTOCOL_IR,
    PROTOCOL_LORA,
} protocol_id_t;

extern ui_theme_t current_theme;

void ui_theme_init(void);
void ui_theme_load_idx(int color_idx);
void ui_theme_load_from_name(const char * theme_name);
void ui_theme_load_from_sd(void);
void ui_theme_set_protocol(protocol_id_t protocol);
lv_color_t ui_theme_get_accent(void);

#endif