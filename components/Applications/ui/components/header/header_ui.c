#include "header_ui.h"
#include "esp_log.h"
#include <stdio.h>

#define HEADER_HEIGHT 24
#define COLOR_PURPLE_TOP     0x1B1764
#define COLOR_PURPLE_BOTTOM  0x1A053D
#define HEADER_BORDER_COLOR  0x5E12A0
#define ICON_COLOR           0xFFFFFF

static const char *TAG = "HEADER_UI";

void header_ui_create(lv_obj_t * parent)
{
    lv_obj_t * header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_PURPLE_TOP), 0);
    lv_obj_set_style_bg_grad_color(header, lv_color_hex(COLOR_PURPLE_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(header, 128, 0);
    lv_obj_set_style_bg_grad_stop(header, 128, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(HEADER_BORDER_COLOR), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_t * lbl_menu = lv_label_create(header);
    lv_label_set_text(lbl_menu, "HOME MENU");
    lv_obj_set_style_text_color(lbl_menu, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_menu, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_menu, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t * icon_cont = lv_obj_create(header);
    lv_obj_set_size(icon_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont, LV_ALIGN_RIGHT_MID, -8, 0);
    
    lv_obj_set_flex_flow(icon_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont, 0, 0);
    lv_obj_set_style_pad_all(icon_cont, 0, 0);
    lv_obj_set_style_pad_column(icon_cont, 10, 0);

    const char * symbols[] = {
        LV_SYMBOL_SD_CARD,
        LV_SYMBOL_AUDIO,
        LV_SYMBOL_BLUETOOTH,
        LV_SYMBOL_BATTERY_FULL
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t * icn = lv_label_create(icon_cont);
        lv_label_set_text(icn, symbols[i]);
        lv_obj_set_style_text_color(icn, lv_color_hex(ICON_COLOR), 0);
        lv_obj_set_style_text_font(icn, &lv_font_montserrat_14, 0); 
    }
}