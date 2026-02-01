#include "wifi_sniffer_raw_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "wifi_sniffer.h"
#include "buzzer.h"
#include "msgbox_ui.h"
#include "storage_init.h"
#include "storage_impl.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>
#include "lvgl.h"

static lv_obj_t * screen_sniffer = NULL;
static lv_obj_t * ta_term = NULL;
static lv_obj_t * btn_save = NULL;
static lv_timer_t * update_timer = NULL;
static char current_filename[32];
static bool is_running = false;
static bool pending_restart = false;
static uint8_t dot_tick = 0;
#define PCAP_DIR "/assets/storage/wifi/pcap"

extern lv_group_t * main_group;

static void msgbox_closed_cb(bool confirm);
static void msgbox_after_close_cb(void *user_data);

static void update_terminal_text(uint32_t bytes) {
    if (!ta_term) return;
    uint32_t kb = bytes / 1024;
    uint32_t mb = kb / 1024;
    uint32_t mb_frac = (kb % 1024) * 100 / 1024;
    uint32_t packets = wifi_sniffer_get_packet_count();
    const char *state = is_running ? "CAPTURING" : "STOPPED";
    const char *dots[] = { "", ".", "..", "..." };
    char text[192];

    if (mb > 0) {
        snprintf(text, sizeof(text),
                 "File: %s\n"
                 "Size: %lu.%02lu MB\n"
                 "%s%s  Pkts: %lu\n",
                 current_filename,
                 (unsigned long)mb, (unsigned long)mb_frac,
                 state, dots[dot_tick % 4],
                 (unsigned long)packets);
    } else {
        snprintf(text, sizeof(text),
                 "File: %s\n"
                 "Size: %lu KB\n"
                 "%s%s  Pkts: %lu\n",
                 current_filename,
                 (unsigned long)kb,
                 state, dots[dot_tick % 4],
                 (unsigned long)packets);
    }

    lv_textarea_set_text(ta_term, text);
}

static void update_size_label(void) {
    uint32_t bytes = wifi_sniffer_get_buffer_usage();
    update_terminal_text(bytes);
}


static void generate_filename(void) {
    int idx = 1;
    char path[128];
    while (idx < 100) {
        snprintf(current_filename, sizeof(current_filename), "sniffer_%02d.pcap", idx);
        snprintf(path, sizeof(path), "%s/%s", PCAP_DIR, current_filename);
        if (!storage_file_exists(path)) {
            break;
        }
        idx++;
    }
    update_terminal_text(wifi_sniffer_get_buffer_usage());
}

static void save_current_capture(void) {
    if (is_running) {
        wifi_sniffer_stop();
        is_running = false;
    }

    esp_err_t err = wifi_sniffer_save_to_internal_flash(current_filename);
    wifi_sniffer_free_buffer();
    pending_restart = true;

    if (err == ESP_OK) {
        msgbox_open(LV_SYMBOL_OK, "PCAP SAVED", "OK", NULL, msgbox_closed_cb);
    } else {
        char msg[64];
        const char *reason = esp_err_to_name(err);
        if (err == ESP_ERR_INVALID_STATE) {
            reason = "STORAGE NOT MOUNTED";
        }
        snprintf(msg, sizeof(msg), "SAVE FAILED\n%s", reason);
        msgbox_open(LV_SYMBOL_CLOSE, msg, "OK", NULL, msgbox_closed_cb);
    }
}

static void update_timer_cb(lv_timer_t * t) {
    (void)t;
    dot_tick++;
    update_terminal_text(wifi_sniffer_get_buffer_usage());
}

static void restore_focus(void) {
    if (main_group && btn_save) {
        lv_group_remove_all_objs(main_group);
        lv_group_add_obj(main_group, btn_save);
        lv_group_focus_obj(btn_save);
        lv_group_set_editing(main_group, false);
    }
}

static void restart_capture(void) {
    generate_filename();
    update_size_label();
    if (wifi_sniffer_start(SNIFF_TYPE_RAW, 0)) {
        is_running = true;
    } else {
        is_running = false;
    }
}

static void msgbox_closed_cb(bool confirm) {
    (void)confirm;
    if (pending_restart) {
        lv_async_call(msgbox_after_close_cb, NULL);
    }
}

static void msgbox_after_close_cb(void *user_data) {
    (void)user_data;
    if (!pending_restart) return;
    pending_restart = false;
    restart_capture();
    restore_focus();
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (update_timer) {
            lv_timer_del(update_timer);
            update_timer = NULL;
        }
        if (is_running) {
            wifi_sniffer_stop();
            is_running = false;
        }
        wifi_sniffer_free_buffer();
        buzzer_play_sound_file("buzzer_click");
        ui_switch_screen(SCREEN_WIFI_PACKETS_MENU);
    } else if (key == LV_KEY_ENTER) {
        save_current_capture();
    }
}

static void save_button_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY && lv_event_get_key(e) == LV_KEY_ENTER) {
        save_current_capture();
    }
}

void ui_wifi_sniffer_raw_open(void) {
    if (screen_sniffer) lv_obj_del(screen_sniffer);
    if (update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }

    screen_sniffer = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_sniffer, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_sniffer, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_sniffer);
    footer_ui_create(screen_sniffer);

    ta_term = lv_textarea_create(screen_sniffer);
    lv_obj_set_size(ta_term, 230, 125);
    lv_obj_align(ta_term, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_text_font(ta_term, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(ta_term, lv_color_black(), 0);
    lv_obj_set_style_text_color(ta_term, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_radius(ta_term, 0, 0);
    lv_obj_set_style_border_width(ta_term, 1, 0);
    lv_obj_set_style_border_color(ta_term, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(ta_term, 1, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(ta_term, lv_color_hex(0x00FF00), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(ta_term, 0, LV_STATE_FOCUS_KEY);
    lv_textarea_set_text(ta_term, "Listening...\n");

    btn_save = lv_btn_create(screen_sniffer);
    lv_obj_set_size(btn_save, 120, 28);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_obj_set_style_bg_color(btn_save, current_theme.bg_item_bot, 0);
    lv_obj_set_style_bg_grad_color(btn_save, current_theme.bg_item_top, 0);
    lv_obj_set_style_bg_grad_dir(btn_save, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(btn_save, 1, 0);
    lv_obj_set_style_border_color(btn_save, current_theme.border_inactive, 0);
    lv_obj_set_style_radius(btn_save, 4, 0);
    lv_obj_set_style_text_color(btn_save, lv_color_white(), 0);
    lv_obj_set_style_border_color(btn_save, current_theme.border_accent, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn_save, 2, LV_STATE_FOCUS_KEY);

    lv_obj_t * lbl_btn = lv_label_create(btn_save);
    lv_label_set_text(lbl_btn, "STOP & SAVE");
    lv_obj_center(lbl_btn);

    lv_obj_add_event_cb(btn_save, save_button_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(btn_save, screen_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(ta_term, screen_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_sniffer, screen_event_cb, LV_EVENT_KEY, NULL);

    generate_filename();
    update_size_label();

    if (wifi_sniffer_start(SNIFF_TYPE_RAW, 0)) {
        is_running = true;
    } else {
        is_running = false;
    }

    update_timer = lv_timer_create(update_timer_cb, 500, NULL);

    if (main_group) {
        lv_group_remove_all_objs(main_group);
        lv_group_add_obj(main_group, ta_term);
        lv_group_add_obj(main_group, btn_save);
        lv_group_focus_obj(ta_term);
    }

    lv_screen_load(screen_sniffer);
}
