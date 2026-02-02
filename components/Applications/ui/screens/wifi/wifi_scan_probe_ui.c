#include "wifi_scan_probe_ui.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "probe_monitor.h"
#include "buzzer.h"
#include "esp_log.h"

static const char *TAG = "UI_PROBE_SCAN";
static lv_obj_t * screen_probe_scan = NULL;
static lv_obj_t * lbl_count = NULL;
static lv_obj_t * ta_log = NULL;
static lv_timer_t * update_timer = NULL;
static bool is_running = false;
static uint16_t last_count = 0;

extern lv_group_t * main_group;

static void update_log(lv_timer_t * t) {
    (void)t;
    if (!is_running) return;

    uint16_t count = 0;
    probe_record_t * results = probe_monitor_get_results(&count);

    if (!results) return;
    if (count == last_count) return;

    lv_label_set_text_fmt(lbl_count, "Probes: %d", count);

    lv_textarea_set_text(ta_log, "");
    int start_idx = (count > 15) ? count - 15 : 0;
    for (int i = start_idx; i < count; i++) {
        char line[96];
        snprintf(
            line,
            sizeof(line),
            "[%02X:%02X:%02X:%02X:%02X:%02X] -> \"%s\"\n",
            results[i].mac[0], results[i].mac[1], results[i].mac[2],
            results[i].mac[3], results[i].mac[4], results[i].mac[5],
            results[i].ssid
        );
        lv_textarea_add_text(ta_log, line);
    }
    last_count = count;
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if (is_running) {
                probe_monitor_stop();
                is_running = false;
            }
            if (update_timer) {
                lv_timer_del(update_timer);
                update_timer = NULL;
            }
            buzzer_play_sound_file("buzzer_click");
            ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
        }
    }
}

void ui_wifi_scan_probe_open(void) {
    if (screen_probe_scan) lv_obj_del(screen_probe_scan);

    screen_probe_scan = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_probe_scan, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_probe_scan, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_probe_scan);
    footer_ui_create(screen_probe_scan);

    lbl_count = lv_label_create(screen_probe_scan);
    lv_label_set_text(lbl_count, "Probes: 0");
    lv_obj_set_style_text_color(lbl_count, lv_color_white(), 0);
    lv_obj_align(lbl_count, LV_ALIGN_TOP_MID, 0, 30);

    ta_log = lv_textarea_create(screen_probe_scan);
    lv_obj_set_size(ta_log, 230, 150);
    lv_obj_align(ta_log, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_text_font(ta_log, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(ta_log, lv_color_black(), 0);
    lv_obj_set_style_text_color(ta_log, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_radius(ta_log, 0, 0);
    lv_obj_set_style_border_width(ta_log, 1, 0);
    lv_obj_set_style_border_color(ta_log, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(ta_log, 1, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(ta_log, lv_color_hex(0x00FF00), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(ta_log, 0, LV_STATE_FOCUS_KEY);
    lv_textarea_set_text(ta_log, "Listening...\n");

    lv_obj_add_event_cb(screen_probe_scan, screen_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(ta_log, screen_event_cb, LV_EVENT_KEY, NULL);

    if (!is_running) {
        probe_monitor_start();
        is_running = true;
        last_count = 0;
    }

    if (update_timer) lv_timer_del(update_timer);
    update_timer = lv_timer_create(update_log, 500, NULL);

    if (main_group) {
        lv_group_add_obj(main_group, ta_log);
        lv_group_focus_obj(ta_log);
    }

    lv_screen_load(screen_probe_scan);
}
