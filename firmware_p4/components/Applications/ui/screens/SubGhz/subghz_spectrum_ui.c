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
static lv_obj_t * lbl_freq_info = NULL;
static int32_t s_chart_points[SPECTRUM_SAMPLES];

#define SPECTRUM_CENTER_FREQ  433920000
#define SPECTRUM_SPAN_HZ      2000000

static void update_spectrum_cb(lv_timer_t * t) {
    if (!chart || !ser_rssi) return;

    subghz_spectrum_line_t line;
    if (!subghz_spectrum_get_line(&line)) return;

    float max_dbm = -130.0;
    uint32_t peak_freq = line.start_freq;

    for (int i = 0; i < SPECTRUM_SAMPLES; i++) {
        int32_t val = (int32_t)(line.dbm_values[i] + 130);
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        s_chart_points[i] = val;

        if (line.dbm_values[i] > max_dbm) {
            max_dbm = line.dbm_values[i];
            peak_freq = line.start_freq + (i * line.step_hz);
        }
    }

    lv_chart_set_ext_y_array(chart, ser_rssi, s_chart_points);
    lv_chart_refresh(chart);

    if (lbl_rssi_info) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Peak: %.1f dBm", max_dbm);
        lv_label_set_text(lbl_rssi_info, buf);

        if (max_dbm > -60) {
            lv_obj_set_style_text_color(lbl_rssi_info, lv_color_hex(0xFFFF00), 0);
        } else {
            lv_obj_set_style_text_color(lbl_rssi_info, lv_color_hex(0x00FF00), 0);
        }
    }

    if (lbl_freq_info) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f MHz", peak_freq / 1000000.0f);
        lv_label_set_text(lbl_freq_info, buf);
    }
}

static void spectrum_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if(key == LV_KEY_ESC) {
            if (update_timer) {
                lv_timer_del(update_timer);
                update_timer = NULL;
            }

            chart = NULL;
            ser_rssi = NULL;
            lbl_rssi_info = NULL;
            lbl_freq_info = NULL;

            subghz_spectrum_stop();

            ui_switch_screen(SCREEN_MENU);
        }
    }
}

void ui_subghz_spectrum_open(void) {
    subghz_spectrum_start(SPECTRUM_CENTER_FREQ, SPECTRUM_SPAN_HZ);

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
    lv_chart_set_point_count(chart, SPECTRUM_SAMPLES);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_CIRCULAR);

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
    lbl_freq_info = lv_label_create(screen_spectrum);
    lv_label_set_text(lbl_freq_info, "433.92 MHz");
    lv_obj_set_style_text_font(lbl_freq_info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_freq_info, lv_color_hex(0xCC88FF), 0);
    lv_obj_align(lbl_freq_info, LV_ALIGN_BOTTOM_LEFT, 0, -35);

    lbl_rssi_info = lv_label_create(screen_spectrum);
    lv_label_set_text(lbl_rssi_info, "Peak: --- dBm");
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
