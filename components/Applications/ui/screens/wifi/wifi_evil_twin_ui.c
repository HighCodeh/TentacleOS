// Copyright (c) 2025 HIGH CODE LLC
#include "wifi_evil_twin_ui.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "evil_twin.h"
#include "keyboard_ui.h"
#include "esp_log.h"
#include "buzzer.h"

static const char *TAG = "UI_EVIL_TWIN";
static lv_obj_t * screen_et = NULL;
static lv_obj_t * ta_ssid = NULL;
static lv_obj_t * btn_start = NULL;
static lv_obj_t * lbl_status = NULL;
static bool is_running = false;

extern lv_group_t * main_group;

static void toggle_attack_handler(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
        if (is_running) {
            evil_twin_stop_attack();
            is_running = false;
            lv_label_set_text(lv_obj_get_child(btn_start, 0), "START AP");
            lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x004400), 0);
            lv_label_set_text(lbl_status, "Status: STOPPED");
            if (ta_ssid) lv_obj_clear_state(ta_ssid, LV_STATE_DISABLED);
            buzzer_play_sound_file("buzzer_click");
        } else {
            const char * ssid = lv_textarea_get_text(ta_ssid);
            if (strlen(ssid) == 0) {
                lv_label_set_text(lbl_status, "Invalid SSID!");
                return;
            }
            evil_twin_start_attack(ssid);
            is_running = true;
            lv_label_set_text(lv_obj_get_child(btn_start, 0), "STOP AP");
            lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x440000), 0);
            lv_label_set_text(lbl_status, "Status: AP RUNNING");
            if (ta_ssid) lv_obj_add_state(ta_ssid, LV_STATE_DISABLED);
            buzzer_play_sound_file("buzzer_hacker_confirm");
        }
    }
}

static void ta_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED || (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER)) {
        if (!is_running) {
            keyboard_open(ta_ssid);
        }
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY) {
        if (lv_event_get_key(e) == LV_KEY_ESC) {
            if (is_running) {
                evil_twin_stop_attack();
                is_running = false;
            }
            ui_switch_screen(SCREEN_WIFI_MENU);
        }
    }
}

void ui_wifi_evil_twin_open(void) {
    if (screen_et) lv_obj_del(screen_et);

    screen_et = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_et, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_et, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_et);
    footer_ui_create(screen_et);

    lv_obj_t * title = lv_label_create(screen_et);
    lv_label_set_text(title, "EVIL TWIN AP");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFA500), 0); // Orange
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    ta_ssid = lv_textarea_create(screen_et);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_placeholder_text(ta_ssid, "Enter SSID...");
    lv_textarea_set_text(ta_ssid, "Free WiFi");
    lv_obj_set_width(ta_ssid, 200);
    lv_obj_align(ta_ssid, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

    btn_start = lv_btn_create(screen_et);
    lv_obj_set_size(btn_start, 140, 40);
    lv_obj_align(btn_start, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x004400), 0);
    
    lv_obj_t * lbl_btn = lv_label_create(btn_start);
    lv_label_set_text(lbl_btn, "START AP");
    lv_obj_center(lbl_btn);

    lbl_status = lv_label_create(screen_et);
    lv_label_set_text(lbl_status, "Status: READY");
    lv_obj_set_style_text_color(lbl_status, lv_color_white(), 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_add_event_cb(btn_start, toggle_attack_handler, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_et, screen_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, ta_ssid);
        lv_group_add_obj(main_group, btn_start);
        lv_group_focus_obj(ta_ssid);
    }

    lv_screen_load(screen_et);
}

void ui_wifi_evil_twin_set_ssid(const char *ssid) {
    // Placeholder if we want to preload from scan list later
}
