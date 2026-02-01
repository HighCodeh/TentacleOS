#include "wifi_probe_flood_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "wifi_flood.h"
#include "buzzer.h"
#include "lvgl.h"

static lv_obj_t * screen_probe_flood = NULL;
static lv_obj_t * btn_toggle = NULL;
static lv_obj_t * lbl_status = NULL;
static lv_obj_t * lbl_channel = NULL;
static bool is_running = false;
static uint8_t current_channel = 1;

extern lv_group_t * main_group;

static void update_ui(void) {
    if (lbl_status) {
        lv_label_set_text(lbl_status, is_running ? "Status: FLOODING..." : "Status: IDLE");
        lv_obj_set_style_text_color(lbl_status, current_theme.text_main, 0);
    }
    if (lbl_channel) {
        lv_label_set_text_fmt(lbl_channel, "Channel: %d", current_channel);
    }
    if (btn_toggle) {
        lv_label_set_text(lv_obj_get_child(btn_toggle, 0), is_running ? "STOP" : "START");
        lv_obj_set_style_bg_color(btn_toggle, lv_color_hex(0x5A2CA0), 0);
    }
}

static void toggle_handler(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    if (lv_event_get_key(e) != LV_KEY_ENTER) return;

    if (is_running) {
        wifi_flood_stop();
        is_running = false;
        buzzer_play_sound_file("buzzer_click");
    } else {
        wifi_flood_probe_start(NULL, current_channel);
        is_running = true;
        buzzer_play_sound_file("buzzer_hacker_confirm");
    }
    update_ui();
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (is_running) {
            wifi_flood_stop();
            is_running = false;
        }
        buzzer_play_sound_file("buzzer_click");
        ui_switch_screen(SCREEN_WIFI_ATTACK_MENU);
    } else if (!is_running && (key == LV_KEY_RIGHT || key == LV_KEY_UP || key == LV_KEY_DOWN)) {
        if (key == LV_KEY_RIGHT || key == LV_KEY_UP) current_channel = (current_channel % 13) + 1;
        else current_channel = (current_channel == 1) ? 13 : (current_channel - 1);
        update_ui();
    }
}

void ui_wifi_probe_flood_open(void) {
    if (screen_probe_flood) lv_obj_del(screen_probe_flood);

    screen_probe_flood = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_probe_flood, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_probe_flood, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_probe_flood);
    footer_ui_create(screen_probe_flood);

    lbl_status = lv_label_create(screen_probe_flood);
    lv_label_set_text(lbl_status, "Status: IDLE");
    lv_obj_set_style_text_color(lbl_status, current_theme.text_main, 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, -30);

    lbl_channel = lv_label_create(screen_probe_flood);
    lv_label_set_text_fmt(lbl_channel, "Channel: %d", current_channel);
    lv_obj_set_style_text_color(lbl_channel, current_theme.text_main, 0);
    lv_obj_align(lbl_channel, LV_ALIGN_CENTER, 0, -10);

    btn_toggle = lv_btn_create(screen_probe_flood);
    lv_obj_set_size(btn_toggle, 140, 40);
    lv_obj_align(btn_toggle, LV_ALIGN_CENTER, 0, 45);
    lv_obj_set_style_bg_color(btn_toggle, lv_color_hex(0x5A2CA0), 0);
    lv_obj_set_style_border_color(btn_toggle, current_theme.border_accent, 0);
    lv_obj_set_style_border_width(btn_toggle, 2, 0);

    lv_obj_t * lbl_btn = lv_label_create(btn_toggle);
    lv_label_set_text(lbl_btn, "START");
    lv_obj_center(lbl_btn);

    lv_obj_add_event_cb(btn_toggle, toggle_handler, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(btn_toggle, screen_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_probe_flood, screen_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, btn_toggle);
        lv_group_focus_obj(btn_toggle);
    }

    update_ui();
    lv_screen_load(screen_probe_flood);
}
