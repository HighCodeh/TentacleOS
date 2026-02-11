#include "header_ui.h"
#include "ui_theme.h"
#include "wifi_service.h"
#include "esp_log.h"
#include <stdio.h>

#define HEADER_HEIGHT 24

static const char *TAG = "HEADER_UI";
extern int header_idx;

static bool header_wifi_connected = false;
static bool header_wifi_enabled = true;
static lv_obj_t * wifi_icon_label = NULL;
static lv_timer_t * wifi_status_timer = NULL;

static void header_ui_update_wifi_icon(void)
{
    if (!wifi_icon_label) return;

    if (!header_wifi_enabled) {
        lv_label_set_text(wifi_icon_label, LV_SYMBOL_CLOSE);
        lv_obj_set_style_opa(wifi_icon_label, LV_OPA_COVER, 0);
    } else {
        lv_label_set_text(wifi_icon_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_opa(wifi_icon_label, header_wifi_connected ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

static void header_wifi_status_timer_cb(lv_timer_t * timer)
{
    if (!wifi_icon_label) return;

    bool current_active = wifi_service_is_active();
    bool current_connected = wifi_service_is_connected();

    if (current_active != header_wifi_enabled || current_connected != header_wifi_connected) {
        header_wifi_enabled = current_active;
        header_wifi_connected = current_connected;
        header_ui_update_wifi_icon();
    }
}

void header_ui_create(lv_obj_t * parent)
{
    lv_obj_t * header = lv_obj_create(parent);
    lv_obj_set_size(header, lv_pct(100), HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_border_color(header, current_theme.border_interface, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    if (header_idx == 3) {
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
        lv_color_t bg_main = (header_idx == 1) ? current_theme.bg_secondary : current_theme.bg_primary;
        lv_obj_set_style_bg_color(header, bg_main, 0);

        if (header_idx > 0) { // Aplica gradiente para idx 1 e 2
            lv_color_t bg_grad = (header_idx == 1) ? current_theme.bg_primary : current_theme.bg_secondary;
            lv_obj_set_style_bg_grad_color(header, bg_grad, 0);
            lv_obj_set_style_bg_grad_dir(header, LV_GRAD_DIR_VER, 0);
        }
    }
    
    lv_obj_t * lbl_time = lv_label_create(header);
    lv_label_set_text(lbl_time, "12:00");
    lv_obj_set_style_text_color(lbl_time, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 10, 0);
    
    lv_obj_t * icon_cont = lv_obj_create(header);
    lv_obj_set_size(icon_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(icon_cont, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_flex_flow(icon_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icon_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(icon_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_cont, 0, 0);
    lv_obj_set_style_pad_all(icon_cont, 0, 0);
    lv_obj_set_style_pad_column(icon_cont, 10, 0);
    
    wifi_icon_label = lv_label_create(icon_cont);
    lv_obj_set_style_text_color(wifi_icon_label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(wifi_icon_label, &lv_font_montserrat_14, 0);

    header_wifi_enabled = wifi_service_is_active();
    header_wifi_connected = wifi_service_is_connected();
    header_ui_update_wifi_icon();

    lv_obj_t * icn_bat = lv_label_create(icon_cont);
    lv_label_set_text(icn_bat, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(icn_bat, current_theme.text_main, 0);
    lv_obj_set_style_text_font(icn_bat, &lv_font_montserrat_14, 0); 

    if (wifi_status_timer == NULL) {
        wifi_status_timer = lv_timer_create(header_wifi_status_timer_cb, 500, NULL);
    }
}