// Copyright (c) 2025 HIGH CODE LLC
#include "wifi_probe_ui.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "probe_monitor.h"
#include "esp_log.h"
#include "buzzer.h"

static const char *TAG = "UI_PROBE";
static lv_obj_t * screen_probe = NULL;
static lv_obj_t * btn_toggle = NULL;
static lv_obj_t * lbl_count = NULL;
static lv_obj_t * ta_log = NULL;
static lv_timer_t * update_timer = NULL;
static bool is_running = false;
static uint16_t last_count = 0;

extern lv_group_t * main_group;

static void update_log(lv_timer_t * t) {
    if (!is_running) return;

    uint16_t count = 0;
    probe_record_t * results = probe_monitor_get_results(&count);

    if (count != last_count) {
        lv_label_set_text_fmt(lbl_count, "Devices: %d", count);
        
        // Append only new ones if possible, or just refresh the tail
        // Simple approach: Clear and show last 10
        lv_textarea_set_text(ta_log, "");
        
        int start_idx = (count > 10) ? count - 10 : 0;
        for (int i = start_idx; i < count; i++) {
            char line[64];
            snprintf(line, sizeof(line), "[%d] %s (%ddBm)\n", 
                     i, results[i].ssid, results[i].rssi);
            lv_textarea_add_text(ta_log, line);
        }
        last_count = count;
    }
}

static void toggle_handler(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
        if (is_running) {
            probe_monitor_stop();
            is_running = false;
            lv_label_set_text(lv_obj_get_child(btn_toggle, 0), "START MONITOR");
            lv_obj_set_style_bg_color(btn_toggle, lv_color_hex(0x004400), 0);
            if (update_timer) lv_timer_pause(update_timer);
            buzzer_play_sound_file("buzzer_click");
        } else {
            probe_monitor_start();
            is_running = true;
            last_count = 0;
            lv_label_set_text(lv_obj_get_child(btn_toggle, 0), "STOP MONITOR");
            lv_obj_set_style_bg_color(btn_toggle, lv_color_hex(0x440000), 0);
            lv_textarea_set_text(ta_log, "Listening...\n");
            if (update_timer) lv_timer_resume(update_timer);
            buzzer_play_sound_file("buzzer_hacker_confirm");
        }
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY) {
        if (lv_event_get_key(e) == LV_KEY_ESC) {
            if (is_running) {
                probe_monitor_stop();
                is_running = false;
            }
            if (update_timer) {
                lv_timer_del(update_timer);
                update_timer = NULL;
            }
            ui_switch_screen(SCREEN_WIFI_MENU);
        }
    }
}

void ui_wifi_probe_open(void) {
    if (screen_probe) lv_obj_del(screen_probe);

    screen_probe = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_probe, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_probe, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_probe);
    footer_ui_create(screen_probe);

    lv_obj_t * title = lv_label_create(screen_probe);
    lv_label_set_text(title, "PROBE MONITOR");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF00FF), 0); // Magenta
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lbl_count = lv_label_create(screen_probe);
    lv_label_set_text(lbl_count, "Devices: 0");
    lv_obj_set_style_text_color(lbl_count, lv_color_white(), 0);
    lv_obj_align(lbl_count, LV_ALIGN_TOP_RIGHT, -10, 30);

    ta_log = lv_textarea_create(screen_probe);
    lv_obj_set_size(ta_log, 220, 100);
    lv_obj_align(ta_log, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_text_font(ta_log, &lv_font_montserrat_14, 0); // Small font
    lv_obj_set_style_bg_color(ta_log, lv_color_black(), 0);
    lv_obj_set_style_text_color(ta_log, lv_color_hex(0x00FF00), 0);
    lv_textarea_set_text(ta_log, "Ready.");

    btn_toggle = lv_btn_create(screen_probe);
    lv_obj_set_size(btn_toggle, 140, 30);
    lv_obj_align(btn_toggle, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_set_style_bg_color(btn_toggle, lv_color_hex(0x004400), 0);
    
    lv_obj_t * lbl_btn = lv_label_create(btn_toggle);
    lv_label_set_text(lbl_btn, "START MONITOR");
    lv_obj_center(lbl_btn);

    lv_obj_add_event_cb(btn_toggle, toggle_handler, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_probe, screen_event_cb, LV_EVENT_KEY, NULL);

    update_timer = lv_timer_create(update_log, 500, NULL);
    lv_timer_pause(update_timer);

    if (main_group) {
        lv_group_add_obj(main_group, btn_toggle);
        lv_group_focus_obj(btn_toggle);
    }

    lv_screen_load(screen_probe);
}
