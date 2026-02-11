#include "subghz_spectrum_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "subghz_spectrum.h"
#include "lv_conf_internal.h"
#include "ui_manager.h"
#include "esp_log.h"
#include "core/lv_group.h"
#include "lv_port_indev.h"
#include <stdio.h>

static const char *TAG = "SUBGHZ_UI";
static lv_obj_t * screen_spectrum = NULL;
static lv_obj_t * chart = NULL;
static lv_chart_series_t * ser_rssi = NULL;
static lv_timer_t * update_timer = NULL;
static lv_obj_t * lbl_rssi_info = NULL;

static void update_spectrum_cb(lv_timer_t * t) {
    if (!chart || !ser_rssi) return;

    float max_dbm = -120.0;
    for (int i = 0; i < SPECTRUM_SAMPLES; i++) {
        if (spectrum_data[i] > max_dbm) {
            max_dbm = spectrum_data[i];
        }
    }

    int32_t visual_val = (int32_t)(max_dbm + 115); 
    if (visual_val < 0) visual_val = 0;
    if (visual_val > 100) visual_val = 100;

    lv_chart_set_next_value(chart, ser_rssi, visual_val);

    if(lbl_rssi_info) {
        char buf[32];
        snprintf(buf, sizeof(buf), "RSSI: %.1f dBm", max_dbm);
        lv_label_set_text(lbl_rssi_info, buf);
        
        if(max_dbm > -60) {
            lv_obj_set_style_text_color(lbl_rssi_info, lv_color_hex(0xFFFF00), 0); // Amarelo
        } else {
            lv_obj_set_style_text_color(lbl_rssi_info, lv_color_hex(0x00FF00), 0); // Verde
        }
    }
}

static void spectrum_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if(key == LV_KEY_ESC) {
            // Stop UI Timer first to prevent access to freed objects
            if (update_timer) {
                lv_timer_del(update_timer);
                update_timer = NULL;
            }
            
            // Clear pointers
            chart = NULL;
            ser_rssi = NULL;

            // Stop backend
            subghz_spectrum_stop();
            
            // Return to previous menu or main menu. 
            // Assuming Main Menu for now or SubGhz Menu if it existed.
            ui_switch_screen(SCREEN_MENU); 
        }
    }
}

void ui_subghz_spectrum_open(void) {
    // Start the backend task
    subghz_spectrum_start();

    if(screen_spectrum) {
        lv_obj_del(screen_spectrum);
        screen_spectrum = NULL;
    }

    if (update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }

    screen_spectrum = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_spectrum, lv_color_black(), 0);
    lv_obj_remove_flag(screen_spectrum, LV_OBJ_FLAG_SCROLLABLE);

    /* --- CHART --- */
    chart = lv_chart_create(screen_spectrum);
    lv_obj_set_size(chart, 220, 120);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, -10);
    
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE); 
    lv_chart_set_point_count(chart, 100); 
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

    lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR); 
    lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    lv_obj_set_style_bg_color(chart, lv_color_hex(0x110022), 0); 
    lv_obj_set_style_border_color(chart, lv_color_hex(0x5500AA), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    
    ser_rssi = lv_chart_add_series(chart, lv_color_hex(0x00FF00), LV_CHART_AXIS_PRIMARY_Y);
    
    lv_obj_set_style_bg_opa(chart, 80, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x00FF00), LV_PART_ITEMS); 
    lv_obj_set_style_bg_grad_color(chart, lv_color_hex(0x220044), LV_PART_ITEMS); 
    lv_obj_set_style_bg_grad_dir(chart, LV_GRAD_DIR_VER, LV_PART_ITEMS);

    lv_obj_set_style_line_dash_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x440066), LV_PART_MAIN);

    /* --- INFO LABELS --- */
    lv_obj_t * lbl_freq_info = lv_label_create(screen_spectrum);
    lv_label_set_text(lbl_freq_info, "FREQ: 433.92 MHz");
    lv_obj_set_style_text_font(lbl_freq_info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_freq_info, lv_color_hex(0xCC88FF), 0);
    lv_obj_align(lbl_freq_info, LV_ALIGN_BOTTOM_LEFT, 0, -35);

    lbl_rssi_info = lv_label_create(screen_spectrum);
    lv_label_set_text(lbl_rssi_info, "RSSI: --- dBm");
    lv_obj_set_style_text_font(lbl_rssi_info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_rssi_info, lv_color_hex(0x00FF00), 0);
    lv_obj_align(lbl_rssi_info, LV_ALIGN_BOTTOM_RIGHT, 0, -35);

    /* --- COMMON UI --- */
    header_ui_create(screen_spectrum);
    footer_ui_create(screen_spectrum);

    update_timer = lv_timer_create(update_spectrum_cb, 50, NULL);

    lv_obj_add_event_cb(screen_spectrum, spectrum_event_cb, LV_EVENT_KEY, NULL);
    
    if(main_group) {
        lv_group_add_obj(main_group, screen_spectrum);
        lv_group_focus_obj(screen_spectrum);
    }

    lv_screen_load(screen_spectrum);
}

