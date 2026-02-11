#include "wifi_scan_monitor_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "wifi_sniffer.h"
#include "buzzer.h"
#include "lvgl.h"

static lv_obj_t * screen_monitor = NULL;
static lv_obj_t * chart = NULL;
static lv_chart_series_t * ser_pps = NULL;
static lv_timer_t * update_timer = NULL;
static lv_obj_t * lbl_pkts = NULL;
static lv_obj_t * lbl_deauth = NULL;

static uint32_t last_pkt_count = 0;

extern lv_group_t * main_group;

#define CHART_MAX_VAL 500 

static void update_monitor_cb(lv_timer_t * t) {
    (void)t;
    uint32_t pkt_count = wifi_sniffer_get_packet_count();
    uint32_t deauth_count = wifi_sniffer_get_deauth_count();

    uint32_t pps = (pkt_count >= last_pkt_count) ? (pkt_count - last_pkt_count) : pkt_count;
    last_pkt_count = pkt_count;

    if (chart && ser_pps) {
        if (pps > CHART_MAX_VAL) pps = CHART_MAX_VAL;
        lv_chart_set_next_value(chart, ser_pps, pps);
    }

    if (lbl_pkts) {
        lv_label_set_text_fmt(lbl_pkts, "Pkts: %lu", (unsigned long)pkt_count);
    }

    if (lbl_deauth) {
        lv_label_set_text_fmt(lbl_deauth, "Deauths: %lu", (unsigned long)deauth_count);
        if (deauth_count > 0) {
            lv_obj_set_style_text_color(lbl_deauth, lv_color_hex(0xFF3333), 0);
        } else {
            lv_obj_set_style_text_color(lbl_deauth, lv_color_white(), 0);
        }
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (update_timer) {
                lv_timer_del(update_timer);
                update_timer = NULL;
            }
            wifi_sniffer_stop();
            wifi_sniffer_free_buffer();
            buzzer_play_sound_file("buzzer_click");
            ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
        }
    }
}

void ui_wifi_scan_monitor_open(void) {
    if (screen_monitor) lv_obj_del(screen_monitor);
    if (update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }

    wifi_sniffer_start(SNIFF_TYPE_RAW, 0);
    last_pkt_count = 0;

    screen_monitor = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_monitor, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_monitor, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_monitor);
    footer_ui_create(screen_monitor);

    chart = lv_chart_create(screen_monitor);
    lv_obj_set_size(chart, 220, 110);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, -10);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 80);
    
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_MAX_VAL);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

    lv_obj_set_style_bg_color(chart, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x00CC66), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_radius(chart, 0, 0);

    lv_obj_set_style_line_width(chart, 1, LV_PART_ITEMS); 
    lv_obj_set_style_line_color(chart, lv_color_hex(0x00CC66), LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart, false, LV_PART_ITEMS); 
    
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 0, LV_PART_INDICATOR);

    lv_obj_set_style_line_color(chart, lv_color_hex(0x003322), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(chart, false, LV_PART_MAIN);

    ser_pps = lv_chart_add_series(chart, lv_color_hex(0x00CC66), LV_CHART_AXIS_PRIMARY_Y);

    lbl_pkts = lv_label_create(screen_monitor);
    lv_label_set_text(lbl_pkts, "Pkts: 0");
    lv_obj_set_style_text_color(lbl_pkts, lv_color_white(), 0);
    lv_obj_align(lbl_pkts, LV_ALIGN_BOTTOM_LEFT, 6, -35);

    lbl_deauth = lv_label_create(screen_monitor);
    lv_label_set_text(lbl_deauth, "Deauths: 0");
    lv_obj_set_style_text_color(lbl_deauth, lv_color_white(), 0);
    lv_obj_align(lbl_deauth, LV_ALIGN_BOTTOM_RIGHT, -6, -35);

    update_timer = lv_timer_create(update_monitor_cb, 1000, NULL);
    update_monitor_cb(update_timer);

    lv_obj_add_event_cb(screen_monitor, screen_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(chart, screen_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, screen_monitor);
        lv_group_focus_obj(screen_monitor);
    }

    lv_screen_load(screen_monitor);
}
