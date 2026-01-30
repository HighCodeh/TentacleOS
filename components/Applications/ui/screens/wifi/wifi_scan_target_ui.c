#include "wifi_scan_target_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "ap_scanner.h"
#include "target_scanner.h"
#include "buzzer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

typedef enum {
    TARGET_VIEW_APS = 0,
    TARGET_VIEW_CLIENTS = 1
} target_view_t;

static const char *TAG = "UI_WIFI_TARGET";
static lv_obj_t * screen_target = NULL;
static lv_obj_t * list_cont = NULL;
static lv_obj_t * loading_label = NULL;
static lv_obj_t * empty_label = NULL;
static lv_obj_t * title_label = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static target_view_t current_view = TARGET_VIEW_APS;
static wifi_ap_record_t * ap_results = NULL;
static uint16_t ap_count = 0;
static wifi_ap_record_t selected_ap;
static lv_timer_t * update_timer = NULL;
static uint16_t last_client_count = 0;

extern lv_group_t * main_group;

static void ap_scan_task(void *arg);
static void list_event_cb(lv_event_t * e);
static void go_back_or_ap_view(void);
static void start_ap_scan(void);
static void start_client_scan(void);
static void update_clients_cb(lv_timer_t * t);

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
    if (empty_label) {
        lv_obj_del(empty_label);
        empty_label = NULL;
    }
    if (main_group) lv_group_remove_all_objs(main_group);
}

static void set_loading(const char *text) {
    if (!loading_label) {
        loading_label = lv_label_create(screen_target);
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

static void show_ap_title(void) {
    if (!title_label) {
        title_label = lv_label_create(screen_target);
        lv_obj_set_style_text_color(title_label, current_theme.text_main, 0);
        lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 30);
    }
    lv_label_set_text_fmt(title_label, "AP: %s", (char *)selected_ap.ssid);
    lv_obj_clear_flag(title_label, LV_OBJ_FLAG_HIDDEN);
}

static void hide_ap_title(void) {
    if (title_label) {
        lv_obj_add_flag(title_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_empty_center(const char *text) {
    if (empty_label) {
        lv_label_set_text(empty_label, text);
        lv_obj_center(empty_label);
        return;
    }
    empty_label = lv_label_create(screen_target);
    lv_label_set_text(empty_label, text);
    lv_obj_set_style_text_color(empty_label, current_theme.text_main, 0);
    lv_obj_center(empty_label);
    lv_obj_add_event_cb(empty_label, list_event_cb, LV_EVENT_KEY, NULL);
    if (main_group) {
        lv_group_add_obj(main_group, empty_label);
        lv_group_focus_obj(empty_label);
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
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            go_back_or_ap_view();
        } else if (key == LV_KEY_ENTER && current_view == TARGET_VIEW_APS) {
            wifi_ap_record_t * ap = (wifi_ap_record_t *)lv_obj_get_user_data(item);
            if (!ap) return;
            selected_ap = *ap;
            start_client_scan();
        }
    }
}

static void populate_ap_list(wifi_ap_record_t * results, uint16_t count) {
    ap_results = results;
    ap_count = count;
    hide_ap_title();
    if (!results || count == 0) {
        set_empty_center("NO APS FOUND");
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

static void populate_client_list(target_client_record_t * results, uint16_t count) {
    clear_list();
    show_ap_title();
    if (!results || count == 0) {
        return;
    }

    for (uint16_t i = 0; i < count; i++) {
        target_client_record_t * rec = &results[i];
        lv_obj_t * item = lv_obj_create(list_cont);
        lv_obj_set_size(item, lv_pct(100), 40);
        lv_obj_add_style(item, &style_item, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * icon = lv_label_create(item);
        lv_label_set_text(icon, LV_SYMBOL_EYE_OPEN);
        lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

        lv_obj_t * lbl = lv_label_create(item);
        lv_label_set_text_fmt(
            lbl,
            "%02X:%02X:%02X:%02X:%02X:%02X  %ddBm",
            rec->client_mac[0], rec->client_mac[1], rec->client_mac[2],
            rec->client_mac[3], rec->client_mac[4], rec->client_mac[5],
            rec->rssi
        );
        lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
        lv_obj_set_flex_grow(lbl, 1);
        lv_obj_set_style_margin_left(lbl, 8, 0);

        lv_obj_add_event_cb(item, item_focus_cb, LV_EVENT_ALL, NULL);
        if (main_group) lv_group_add_obj(main_group, item);
    }

    if (main_group) {
        lv_obj_t * first = lv_obj_get_child(list_cont, 0);
        if (first) lv_group_focus_obj(first);
    }
}

static void ap_scan_task(void *arg) {
    (void)arg;
    ap_scanner_start();
    uint16_t count = 0;
    wifi_ap_record_t * results = NULL;
    int timeout = 100;
    while (timeout-- > 0) {
        results = ap_scanner_get_results(&count);
        if (results) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (ui_acquire()) {
        clear_loading();
        populate_ap_list(results, count);
        ui_release();
    }
    vTaskDelete(NULL);
}

static void update_clients_cb(lv_timer_t * t) {
    (void)t;
    bool scanning = false;
    uint16_t count = 0;
    target_client_record_t * results = target_scanner_get_live_results(&count, &scanning);

    buzzer_play_sound_file("buzzer_scroll_tick");

    if (!results || count == 0) {
        if (scanning) {
            set_empty_center("SCANNING CLIENTS...");
        } else {
            set_empty_center("NO CLIENTS FOUND");
            if (update_timer) {
                lv_timer_del(update_timer);
                update_timer = NULL;
            }
        }
        return;
    }

    if (empty_label) {
        lv_obj_del(empty_label);
        empty_label = NULL;
    }

    if (count != last_client_count) {
        populate_client_list(results, count);
        last_client_count = count;
    }

    if (!scanning && update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }
}

static void start_ap_scan(void) {
    current_view = TARGET_VIEW_APS;
    clear_list();
    clear_loading();
    hide_ap_title();
    set_loading("SCANNING APS...");
    lv_refr_now(NULL);

    xTaskCreate(ap_scan_task, "WifiTargetAps", 4096, NULL, 5, NULL);
}

static void start_client_scan(void) {
    current_view = TARGET_VIEW_CLIENTS;
    clear_list();
    clear_loading();
    set_empty_center("SCANNING CLIENTS...");
    lv_refr_now(NULL);

    last_client_count = 0;
    if (!target_scanner_start(selected_ap.bssid, selected_ap.primary)) {
        ESP_LOGW(TAG, "Target scan already running.");
    }

    if (update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }
    update_timer = lv_timer_create(update_clients_cb, 5000, NULL);
    update_clients_cb(update_timer);
}

static void go_back_or_ap_view(void) {
    if (current_view == TARGET_VIEW_CLIENTS) {
        if (update_timer) {
            lv_timer_del(update_timer);
            update_timer = NULL;
        }
        start_ap_scan();
    } else {
        buzzer_play_sound_file("buzzer_click");
        ui_switch_screen(SCREEN_WIFI_SCAN_MENU);
    }
}

static void list_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        go_back_or_ap_view();
    }
}

void ui_wifi_scan_target_open(void) {
    init_styles();
    if (screen_target) lv_obj_del(screen_target);

    screen_target = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_target, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_target, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_target);
    footer_ui_create(screen_target);

    list_cont = lv_obj_create(screen_target);
    lv_obj_set_size(list_cont, 230, 160);
    lv_obj_align(list_cont, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_style(list_cont, &style_menu, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
    lv_obj_add_event_cb(list_cont, list_event_cb, LV_EVENT_KEY, NULL);

    lv_screen_load(screen_target);
    lv_refr_now(NULL);

    start_ap_scan();
}
