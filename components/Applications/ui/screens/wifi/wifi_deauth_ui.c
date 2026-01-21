// Copyright (c) 2025 HIGH CODE LLC
#include "wifi_deauth_ui.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "wifi_deauther.h"
#include "esp_log.h"
#include "buzzer.h"

static const char *TAG = "UI_DEAUTH";
static lv_obj_t * screen_deauth = NULL;
static wifi_ap_record_t target_ap;
static bool target_set = false;
static lv_obj_t * btn_start = NULL;
static lv_obj_t * lbl_status = NULL;

extern lv_group_t * main_group;

void ui_wifi_deauth_set_target(wifi_ap_record_t *ap) {
    if (ap) {
        memcpy(&target_ap, ap, sizeof(wifi_ap_record_t));
        target_set = true;
    }
}

static void toggle_attack_handler(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
        if (wifi_deauther_is_running()) {
            wifi_deauther_stop();
            lv_label_set_text(lv_obj_get_child(btn_start, 0), "START ATTACK");
            lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x004400), 0);
            lv_label_set_text(lbl_status, "Status: IDLE");
            buzzer_play_sound_file("buzzer_click");
        } else {
            if (!target_set) {
                lv_label_set_text(lbl_status, "No Target Selected!");
                return;
            }
            // Broadcast Deauth (Reason: Unspecified)
            wifi_deauther_start(&target_ap, DEAUTH_INVALID_AUTH, true);
            lv_label_set_text(lv_obj_get_child(btn_start, 0), "STOP ATTACK");
            lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x440000), 0);
            lv_label_set_text(lbl_status, "Status: ATTACKING...");
            buzzer_play_sound_file("buzzer_hacker_confirm");
        }
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY) {
        if (lv_event_get_key(e) == LV_KEY_ESC) {
            if (wifi_deauther_is_running()) {
                wifi_deauther_stop();
            }
            ui_switch_screen(SCREEN_WIFI_AP_LIST);
        }
    }
}

void ui_wifi_deauth_open(void) {
    if (screen_deauth) lv_obj_del(screen_deauth);

    screen_deauth = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_deauth, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_deauth, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_deauth);
    footer_ui_create(screen_deauth);

    lv_obj_t * title = lv_label_create(screen_deauth);
    lv_label_set_text(title, "DEAUTH ATTACK");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF0000), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t * lbl_target = lv_label_create(screen_deauth);
    if (target_set) {
        lv_label_set_text_fmt(lbl_target, "Target: %s\nCh: %d  RSSI: %d", target_ap.ssid, target_ap.primary, target_ap.rssi);
    } else {
        lv_label_set_text(lbl_target, "No Target Selected");
    }
    lv_obj_set_style_text_align(lbl_target, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl_target, current_theme.text_main, 0);
    lv_obj_align(lbl_target, LV_ALIGN_CENTER, 0, -20);

    btn_start = lv_btn_create(screen_deauth);
    lv_obj_set_size(btn_start, 140, 40);
    lv_obj_align(btn_start, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x004400), 0);
    lv_obj_set_style_border_color(btn_start, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_width(btn_start, 2, 0);
    
    lv_obj_t * lbl_btn = lv_label_create(btn_start);
    lv_label_set_text(lbl_btn, "START ATTACK");
    lv_obj_center(lbl_btn);

    lbl_status = lv_label_create(screen_deauth);
    lv_label_set_text(lbl_status, "Status: READY");
    lv_obj_set_style_text_color(lbl_status, lv_color_white(), 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_add_event_cb(btn_start, toggle_attack_handler, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_deauth, screen_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, btn_start);
        lv_group_focus_obj(btn_start);
    }

    lv_screen_load(screen_deauth);
}
