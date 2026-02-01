#include "wifi_sniffer_handshake_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "wifi_sniffer.h"
#include "storage_impl.h"
#include "buzzer.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static lv_obj_t * screen_hs = NULL;
static lv_obj_t * ta_term = NULL;
static lv_timer_t * update_timer = NULL;
static bool is_scanning = false;
static bool capture_done = false;
static char current_filename[32];

extern lv_group_t * main_group;

#define PCAP_DIR "/assets/storage/wifi/pcap"

static void generate_filename(void) {
    int idx = 1;
    char path[128];
    while (idx < 100) {
        snprintf(current_filename, sizeof(current_filename), "handshake_%02d.pcap", idx);
        snprintf(path, sizeof(path), "%s/%s", PCAP_DIR, current_filename);
        if (!storage_file_exists(path)) {
            break;
        }
        idx++;
    }
}

static void update_terminal_text(void) {
    if (!ta_term) return;
    const char *state = capture_done ? "HANDSHAKE VALIDADO! (M1+M2)" : "Waiting for connection...";
    char text[192];
    snprintf(text, sizeof(text),
             "Mode: EAPOL Monitor\n"
             "File: %s\n"
             "%s\n"
             "Pkts: %lu\n",
             current_filename,
             state,
             (unsigned long)wifi_sniffer_get_packet_count());
    lv_textarea_set_text(ta_term, text);
}

static void stop_sniffer(void) {
    if (update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }
    if (is_scanning) {
        wifi_sniffer_stop();
        is_scanning = false;
    }
    wifi_sniffer_free_buffer();
}

static void update_timer_cb(lv_timer_t * t) {
    (void)t;
    update_terminal_text();

    if (!capture_done && wifi_sniffer_handshake_captured()) {
        wifi_sniffer_stop();
        is_scanning = false;
        capture_done = true;
        wifi_sniffer_save_to_internal_flash(current_filename);
        wifi_sniffer_free_buffer();
        update_terminal_text();
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        stop_sniffer();
        buzzer_play_sound_file("buzzer_click");
        ui_switch_screen(SCREEN_WIFI_PACKETS_MENU);
    }
}

void ui_wifi_sniffer_handshake_open(void) {
    if (screen_hs) lv_obj_del(screen_hs);
    if (update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }

    screen_hs = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_hs, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_hs, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_hs);
    footer_ui_create(screen_hs);

    ta_term = lv_textarea_create(screen_hs);
    lv_obj_set_size(ta_term, 230, 125);
    lv_obj_align(ta_term, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(ta_term, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(ta_term, lv_color_black(), 0);
    lv_obj_set_style_text_color(ta_term, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_radius(ta_term, 0, 0);
    lv_obj_set_style_border_width(ta_term, 1, 0);
    lv_obj_set_style_border_color(ta_term, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(ta_term, 1, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(ta_term, lv_color_hex(0x00FF00), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(ta_term, 0, LV_STATE_FOCUS_KEY);

    generate_filename();
    wifi_sniffer_clear_handshake();
    capture_done = false;

    if (wifi_sniffer_start(SNIFF_TYPE_EAPOL, 0)) {
        is_scanning = true;
    } else {
        is_scanning = false;
    }

    update_terminal_text();
    update_timer = lv_timer_create(update_timer_cb, 500, NULL);

    lv_obj_add_event_cb(ta_term, screen_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_hs, screen_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_remove_all_objs(main_group);
        lv_group_add_obj(main_group, ta_term);
        lv_group_focus_obj(ta_term);
    }

    lv_screen_load(screen_hs);
}
