// Copyright (c) 2025 HIGH CODE LLC
#include "wifi_beacon_spam_ui.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "beacon_spam.h"
#include "esp_log.h"
#include "buzzer.h"

static const char *TAG = "UI_BEACON_SPAM";
static lv_obj_t * screen_spam = NULL;
static lv_obj_t * btn_mode = NULL;
static lv_obj_t * btn_start = NULL;
static lv_obj_t * lbl_status = NULL;
static bool is_random_mode = true;

extern lv_group_t * main_group;

#define DEFAULT_LIST_PATH "/assets/storage/wifi/beacon_list.json"

static void update_mode_label(void) {
    if (btn_mode) {
        lv_label_set_text_fmt(lv_obj_get_child(btn_mode, 0), "Mode: %s", is_random_mode ? "RANDOM" : "LIST");
    }
}

static void toggle_mode_handler(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY && (lv_event_get_key(e) == LV_KEY_ENTER || lv_event_get_key(e) == LV_KEY_RIGHT || lv_event_get_key(e) == LV_KEY_LEFT)) {
        if (!beacon_spam_is_running()) {
            is_random_mode = !is_random_mode;
            update_mode_label();
            buzzer_play_sound_file("buzzer_scroll_tick");
        }
    }
}

static void toggle_start_handler(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
        if (beacon_spam_is_running()) {
            beacon_spam_stop();
            lv_label_set_text(lv_obj_get_child(btn_start, 0), "START SPAM");
            lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x004400), 0);
            lv_label_set_text(lbl_status, "Status: STOPPED");
            if (btn_mode) lv_obj_clear_state(btn_mode, LV_STATE_DISABLED);
            buzzer_play_sound_file("buzzer_click");
        } else {
            bool success = false;
            if (is_random_mode) {
                success = beacon_spam_start_random();
            } else {
                success = beacon_spam_start_custom(DEFAULT_LIST_PATH);
            }

            if (success) {
                lv_label_set_text(lv_obj_get_child(btn_start, 0), "STOP SPAM");
                lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x440000), 0);
                lv_label_set_text(lbl_status, "Status: SPAMMING...");
                if (btn_mode) lv_obj_add_state(btn_mode, LV_STATE_DISABLED);
                buzzer_play_sound_file("buzzer_hacker_confirm");
            } else {
                lv_label_set_text(lbl_status, "Failed to start!");
                buzzer_play_sound_file("buzzer_error");
            }
        }
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY) {
        if (lv_event_get_key(e) == LV_KEY_ESC) {
            if (beacon_spam_is_running()) {
                beacon_spam_stop();
            }
            ui_switch_screen(SCREEN_WIFI_MENU);
        }
    }
}

void ui_wifi_beacon_spam_open(void) {
    if (screen_spam) lv_obj_del(screen_spam);

    screen_spam = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_spam, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_spam, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_spam);
    footer_ui_create(screen_spam);

    lv_obj_t * title = lv_label_create(screen_spam);
    lv_label_set_text(title, "BEACON SPAM");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FFFF), 0); // Cyan
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    btn_mode = lv_btn_create(screen_spam);
    lv_obj_set_size(btn_mode, 160, 40);
    lv_obj_align(btn_mode, LV_ALIGN_CENTER, 0, -20);
    
    lv_obj_t * lbl_mode = lv_label_create(btn_mode);
    lv_obj_center(lbl_mode);
    update_mode_label();

    btn_start = lv_btn_create(screen_spam);
    lv_obj_set_size(btn_start, 160, 40);
    lv_obj_align(btn_start, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x004400), 0);
    
    lv_obj_t * lbl_btn = lv_label_create(btn_start);
    lv_label_set_text(lbl_btn, "START SPAM");
    lv_obj_center(lbl_btn);

    lbl_status = lv_label_create(screen_spam);
    lv_label_set_text(lbl_status, "Status: READY");
    lv_obj_set_style_text_color(lbl_status, lv_color_white(), 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_add_event_cb(btn_mode, toggle_mode_handler, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(btn_start, toggle_start_handler, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_spam, screen_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, btn_mode);
        lv_group_add_obj(main_group, btn_start);
        lv_group_focus_obj(btn_mode);
    }

    lv_screen_load(screen_spam);
}
