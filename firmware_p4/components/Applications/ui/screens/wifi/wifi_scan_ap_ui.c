#include "wifi_scan_ap_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "ap_scanner.h"
#include "buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "UI_WIFI_SCAN_AP";
static lv_obj_t * screen_scan_ap = NULL;
static lv_obj_t * list_cont = NULL;
static lv_obj_t * loading_label = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

extern lv_group_t * main_group;

static const char * authmode_to_str(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI";
        default: return "UNKNOWN";
    }
}

static lv_opa_t rssi_opa(int8_t rssi) {
    if (rssi >= -50) return LV_OPA_COVER;
    if (rssi >= -60) return LV_OPA_90;
    if (rssi >= -70) return LV_OPA_70;
    if (rssi >= -80) return LV_OPA_50;
    return LV_OPA_30;
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
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            buzzer_play_sound_file("buzzer_click");
            ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
        }
    }
}

static void screen_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
    }
}

static void populate_list(wifi_ap_record_t * results, uint16_t count) {
    if (!list_cont) return;

    if (loading_label) {
        lv_obj_del(loading_label);
        loading_label = NULL;
    }

    if (main_group) lv_group_remove_all_objs(main_group);

    if (!results || count == 0) {
        lv_obj_t * empty = lv_label_create(list_cont);
        lv_label_set_text(empty, "NO NETWORKS FOUND");
        lv_obj_set_style_text_color(empty, current_theme.text_main, 0);
        if (main_group) lv_group_add_obj(main_group, empty);
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        wifi_ap_record_t * ap = &results[i];

        lv_obj_t * item = lv_obj_create(list_cont);
        lv_obj_set_size(item, lv_pct(100), 64);
        lv_obj_add_style(item, &style_item, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * icon = lv_label_create(item);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, current_theme.text_main, 0);
        lv_obj_set_style_text_opa(icon, rssi_opa(ap->rssi), 0);
        lv_obj_set_style_margin_right(icon, 6, 0);

        lv_obj_t * col = lv_obj_create(item);
        lv_obj_set_size(col, lv_pct(100), lv_pct(100));
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_pad_gap(col, 2, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * line1 = lv_label_create(col);
        lv_obj_set_width(line1, lv_pct(100));
        lv_label_set_text_fmt(line1, "%s  CH %d  %ddBm", (char *)ap->ssid, ap->primary, ap->rssi);
        lv_label_set_long_mode(line1, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(line1, current_theme.text_main, 0);

        lv_obj_t * line2 = lv_label_create(col);
        lv_obj_set_width(line2, lv_pct(100));
        lv_label_set_text_fmt(
            line2,
            "MAC %02X:%02X:%02X:%02X:%02X:%02X  %s",
            ap->bssid[0], ap->bssid[1], ap->bssid[2],
            ap->bssid[3], ap->bssid[4], ap->bssid[5],
            authmode_to_str(ap->authmode)
        );
        lv_label_set_long_mode(line2, LV_LABEL_LONG_SCROLL_CIRCULAR);

        if (ap->authmode == WIFI_AUTH_OPEN) {
            lv_obj_set_style_text_color(line2, lv_color_hex(0x00FF00), 0);
        } else {
            lv_obj_set_style_text_color(line2, lv_color_hex(0xFF3333), 0);
        }

        lv_obj_add_event_cb(item, item_focus_cb, LV_EVENT_ALL, NULL);
        if (main_group) lv_group_add_obj(main_group, item);
    }

    if (main_group) {
        lv_obj_t * first = lv_obj_get_child(list_cont, 0);
        if (first) lv_group_focus_obj(first);
    }
}

static void scan_worker_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Starting AP scan...");
    ap_scanner_start();

    uint16_t count = 0;
    int timeout = 100;
    wifi_ap_record_t * results = NULL;
    while (timeout-- > 0) {
        results = ap_scanner_get_results(&count);
        if (results) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (ui_acquire()) {
        populate_list(results, count);
        ui_release();
    }

    vTaskDelete(NULL);
}

void ui_wifi_scan_ap_open(void) {
    init_styles();

    if (screen_scan_ap) lv_obj_del(screen_scan_ap);
    screen_scan_ap = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_scan_ap, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_scan_ap, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_scan_ap);
    footer_ui_create(screen_scan_ap);

    list_cont = lv_obj_create(screen_scan_ap);
    lv_obj_set_size(list_cont, 236, 160);
    lv_obj_align(list_cont, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_style(list_cont, &style_menu, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);

    loading_label = lv_label_create(screen_scan_ap);
    lv_label_set_text(loading_label, "SCANNING...");
    lv_obj_set_style_text_color(loading_label, current_theme.text_main, 0);
    lv_obj_center(loading_label);

    lv_obj_add_event_cb(screen_scan_ap, screen_event_cb, LV_EVENT_KEY, NULL);

    lv_screen_load(screen_scan_ap);
    lv_refr_now(NULL);

    xTaskCreate(scan_worker_task, "WifiScanAp", 4096, NULL, 5, NULL);
}
