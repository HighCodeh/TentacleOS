// Copyright (c) 2025 HIGH CODE LLC
#include "wifi_ap_list_ui.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "ap_scanner.h"
#include "wifi_deauth_ui.h"
#include "wifi_evil_twin_ui.h"
#include "esp_log.h"
#include "buzzer.h"

static const char *TAG = "UI_WIFI_AP_LIST";
static lv_obj_t * screen_list = NULL;
static lv_obj_t * list_obj = NULL;
static wifi_ap_record_t * current_results = NULL;
static uint16_t result_count = 0;

extern lv_group_t * main_group;

static void ap_action_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER) {
            wifi_ap_record_t * ap = (wifi_ap_record_t *)lv_event_get_user_data(e);
            
            // For now, let's just open Deauth with this target
            // Ideally we'd show a menu: Deauth, Evil Twin, etc.
            // I will default to Deauth for this iteration as it's the most requested feature
            
            ESP_LOGI(TAG, "Selected AP: %s", ap->ssid);
            buzzer_play_sound_file("buzzer_hacker_confirm");
            
            ui_wifi_deauth_set_target(ap);
            ui_switch_screen(SCREEN_WIFI_DEAUTH);
        }
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY) {
        if (lv_event_get_key(e) == LV_KEY_ESC) {
            ui_switch_screen(SCREEN_WIFI_MENU);
        }
    }
}

void ui_wifi_ap_list_open(void) {
    if (screen_list) lv_obj_del(screen_list);

    screen_list = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_list, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_list, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_list);
    footer_ui_create(screen_list);

    lv_obj_t * title = lv_label_create(screen_list);
    lv_label_set_text(title, "Select Target");
    lv_obj_set_style_text_color(title, current_theme.text_main, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    list_obj = lv_list_create(screen_list);
    lv_obj_set_size(list_obj, 220, 150);
    lv_obj_align(list_obj, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(list_obj, lv_color_black(), 0);
    lv_obj_set_style_border_color(list_obj, current_theme.border_accent, 0);

    // Get results from Scanner
    current_results = ap_scanner_get_results(&result_count);

    if (current_results && result_count > 0) {
        for (int i = 0; i < result_count; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s (%d) %ddBm", current_results[i].ssid, current_results[i].primary, current_results[i].rssi);
            
            lv_obj_t * btn = lv_list_add_button(list_obj, LV_SYMBOL_WIFI, buf);
            lv_obj_set_style_text_color(btn, lv_color_white(), 0);
            lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
            
            // Pass the AP record pointer directly
            lv_obj_add_event_cb(btn, ap_action_handler, LV_EVENT_KEY, &current_results[i]);
        }
    } else {
        lv_list_add_text(list_obj, "No networks found.");
    }

    lv_obj_add_event_cb(screen_list, screen_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, list_obj);
        lv_group_focus_obj(list_obj);
    }

    lv_screen_load(screen_list);
}
