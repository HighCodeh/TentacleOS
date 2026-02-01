#include "wifi_sniffer_attack_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "wifi_sniffer.h"
#include "wifi_service.h"
#include "storage_impl.h"
#include "msgbox_ui.h"
#include "buzzer.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

typedef enum {
    PMKID_VIEW_APS = 0,
    PMKID_VIEW_SCAN = 1
} pmkid_view_t;

static lv_obj_t * screen_pmkid = NULL;
static lv_obj_t * list_cont = NULL;
static lv_obj_t * loading_label = NULL;
static lv_obj_t * ta_term = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static pmkid_view_t current_view = PMKID_VIEW_APS;
static wifi_ap_record_t selected_ap;
static lv_timer_t * scan_timer = NULL;
static bool is_scanning = false;
static bool capture_done = false;
static char current_filename[32];

extern lv_group_t * main_group;

#define PCAP_DIR "/assets/storage/wifi/pcap"

static void list_event_cb(lv_event_t * e);

static void update_scan_terminal(void) {
    if (!ta_term) return;
    const char *state = capture_done ? "CAPTURED" : (is_scanning ? "SCANNING..." : "STOPPED");
    char text[192];
    snprintf(text, sizeof(text),
             "Target: %s  CH:%d\n"
             "File: %s\n"
             "Status: %s  Pkts:%lu\n",
             selected_ap.ssid, selected_ap.primary,
             current_filename,
             state, (unsigned long)wifi_sniffer_get_packet_count());
    lv_textarea_set_text(ta_term, text);
}

static void init_styles(void) {
    if (styles_initialized) return;

    lv_style_init(&style_menu);
    lv_style_set_bg_color(&style_menu, current_theme.screen_base);
    lv_style_set_bg_opa(&style_menu, LV_OPA_COVER);
    lv_style_set_border_width(&style_menu, 2);
    lv_style_set_border_color(&style_menu, current_theme.border_interface);
    lv_style_set_radius(&style_menu, 0);
    lv_style_set_pad_all(&style_menu, 4);

    lv_style_init(&style_item);
    lv_style_set_bg_color(&style_item, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_item, current_theme.bg_item_top);
    lv_style_set_bg_grad_dir(&style_item, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_item, 1);
    lv_style_set_border_color(&style_item, current_theme.border_inactive);
    lv_style_set_radius(&style_item, 0);

    styles_initialized = true;
}

static void clear_list(void) {
    if (!list_cont) return;
    uint32_t child_count = lv_obj_get_child_count(list_cont);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_del(lv_obj_get_child(list_cont, 0));
    }
    if (main_group) lv_group_remove_all_objs(main_group);
}

static void set_loading(const char *text) {
    if (!loading_label) {
        loading_label = lv_label_create(screen_pmkid);
        lv_obj_set_style_text_color(loading_label, current_theme.text_main, 0);
        lv_obj_center(loading_label);
    }
    lv_label_set_text(loading_label, text);
}

static void clear_loading(void) {
    if (loading_label) {
        lv_obj_del(loading_label);
        loading_label = NULL;
    }
}

static void item_focus_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * item = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        lv_obj_set_style_border_color(item, current_theme.border_accent, 0);
        lv_obj_set_style_border_width(item, 2, 0);
        lv_obj_scroll_to_view(item, LV_ANIM_ON);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_border_color(item, current_theme.border_inactive, 0);
        lv_obj_set_style_border_width(item, 1, 0);
    } else if (code == LV_EVENT_KEY) {
        list_event_cb(e);
    }
}

static void generate_filename(void) {
    int idx = 1;
    char path[128];
    while (idx < 100) {
        snprintf(current_filename, sizeof(current_filename), "pmkid_%02d.pcap", idx);
        snprintf(path, sizeof(path), "%s/%s", PCAP_DIR, current_filename);
        if (!storage_file_exists(path)) {
            break;
        }
        idx++;
    }
    update_scan_terminal();
}

static void stop_scan(void) {
    if (scan_timer) {
        lv_timer_del(scan_timer);
        scan_timer = NULL;
    }
    if (is_scanning) {
        wifi_sniffer_stop();
        is_scanning = false;
    }
    wifi_sniffer_free_buffer();
}

static void scan_tick_cb(lv_timer_t * t) {
    (void)t;
    update_scan_terminal();

    if (!capture_done && wifi_sniffer_pmkid_captured()) {
        wifi_sniffer_stop();
        is_scanning = false;
        capture_done = true;

        esp_err_t err = wifi_sniffer_save_to_internal_flash(current_filename);
        wifi_sniffer_free_buffer();

        if (err == ESP_OK) {
            char msg[128];
            snprintf(msg, sizeof(msg), "PMKID CAPTURED:\n%s", selected_ap.ssid);
            msgbox_open(LV_SYMBOL_OK, msg, "OK", NULL, NULL);
        } else {
            msgbox_open(LV_SYMBOL_CLOSE, "PMKID CAPTURED\nSAVE FAILED", "OK", NULL, NULL);
        }
    }
}

static void show_scan_view(void) {
    clear_list();
    clear_loading();
    current_view = PMKID_VIEW_SCAN;
    capture_done = false;
    wifi_sniffer_clear_pmkid();

    if (list_cont) lv_obj_add_flag(list_cont, LV_OBJ_FLAG_HIDDEN);
    if (ta_term) lv_obj_del(ta_term);

    ta_term = lv_textarea_create(screen_pmkid);
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
    lv_textarea_set_text(ta_term, "Listening...\n");

    generate_filename();

    if (wifi_sniffer_start(SNIFF_TYPE_PMKID, selected_ap.primary)) {
        is_scanning = true;
    } else {
        is_scanning = false;
    }

    if (scan_timer) lv_timer_del(scan_timer);
    scan_timer = lv_timer_create(scan_tick_cb, 500, NULL);
    update_scan_terminal();

    if (main_group) {
        lv_group_remove_all_objs(main_group);
        lv_group_add_obj(main_group, ta_term);
        lv_group_focus_obj(ta_term);
    }

    lv_obj_add_event_cb(ta_term, list_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_pmkid, list_event_cb, LV_EVENT_KEY, NULL);
}

static void populate_ap_list(wifi_ap_record_t * results, uint16_t count) {
    if (!results || count == 0) {
        lv_obj_t * empty = lv_label_create(list_cont);
        lv_label_set_text(empty, "NO APS FOUND");
        lv_obj_set_style_text_color(empty, current_theme.text_main, 0);
        if (main_group) lv_group_add_obj(main_group, empty);
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        wifi_ap_record_t * ap = &results[i];
        lv_obj_t * item = lv_obj_create(list_cont);
        lv_obj_set_size(item, lv_pct(100), 40);
        lv_obj_add_style(item, &style_item, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * icon = lv_label_create(item);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

        lv_obj_t * lbl = lv_label_create(item);
        lv_label_set_text(lbl, (char *)ap->ssid);
        lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
        lv_obj_set_flex_grow(lbl, 1);
        lv_obj_set_style_margin_left(lbl, 8, 0);

        lv_obj_set_user_data(item, ap);
        lv_obj_add_event_cb(item, item_focus_cb, LV_EVENT_ALL, NULL);
        if (main_group) lv_group_add_obj(main_group, item);
    }

    if (main_group) {
        lv_obj_t * first = lv_obj_get_child(list_cont, 0);
        if (first) lv_group_focus_obj(first);
    }
}

static void list_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (current_view == PMKID_VIEW_SCAN) {
            stop_scan();
            current_view = PMKID_VIEW_APS;
            if (ta_term) { lv_obj_del(ta_term); ta_term = NULL; }
            clear_list();
            if (list_cont) lv_obj_clear_flag(list_cont, LV_OBJ_FLAG_HIDDEN);
            set_loading("SCANNING APS...");
            lv_refr_now(NULL);
            if (!wifi_service_is_active()) {
                set_loading("WIFI OFF");
                return;
            }
            wifi_service_scan();
            clear_loading();
            uint16_t count = wifi_service_get_ap_count();
            wifi_ap_record_t *results = (count > 0) ? wifi_service_get_ap_record(0) : NULL;
            populate_ap_list(results, count);
        } else {
            buzzer_play_sound_file("buzzer_click");
            ui_switch_screen(SCREEN_WIFI_PACKETS_MENU);
        }
    } else if (key == LV_KEY_ENTER) {
        if (current_view == PMKID_VIEW_APS) {
            lv_obj_t * focused = lv_group_get_focused(main_group);
            if (!focused) return;
            wifi_ap_record_t * ap = (wifi_ap_record_t *)lv_obj_get_user_data(focused);
            if (!ap) return;
            selected_ap = *ap;
            show_scan_view();
            buzzer_play_sound_file("buzzer_hacker_confirm");
        }
    }
}

void ui_wifi_sniffer_attack_open(void) {
    init_styles();
    if (screen_pmkid) lv_obj_del(screen_pmkid);

    screen_pmkid = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_pmkid, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_pmkid, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_pmkid);
    footer_ui_create(screen_pmkid);

    list_cont = lv_obj_create(screen_pmkid);
    lv_obj_set_size(list_cont, 230, 160);
    lv_obj_align(list_cont, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_style(list_cont, &style_menu, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
    lv_obj_add_event_cb(list_cont, list_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_pmkid, list_event_cb, LV_EVENT_KEY, NULL);

    set_loading("SCANNING APS...");
    lv_screen_load(screen_pmkid);
    lv_refr_now(NULL);

    current_view = PMKID_VIEW_APS;
    if (!wifi_service_is_active()) {
        set_loading("WIFI OFF");
        return;
    }
    wifi_service_scan();
    clear_loading();
    uint16_t count = wifi_service_get_ap_count();
    wifi_ap_record_t *results = (count > 0) ? wifi_service_get_ap_record(0) : NULL;
    populate_ap_list(results, count);
}
