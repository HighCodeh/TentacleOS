#include "wifi_auth_flood_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "core/lv_group.h"
#include "lv_port_indev.h"
#include "wifi_service.h"
#include "wifi_flood.h"
#include "button_ui.h"
#include "buzzer.h"
#include "lvgl.h"

typedef enum {
    AUTH_FLOOD_VIEW_APS = 0,
    AUTH_FLOOD_VIEW_ATTACK = 1
} auth_flood_view_t;

static lv_obj_t * screen_auth = NULL;
static lv_obj_t * list_cont = NULL;
static lv_obj_t * loading_label = NULL;
static lv_obj_t * lbl_target = NULL;
static lv_obj_t * btn_attack = NULL;
static lv_obj_t * lbl_attempts = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static auth_flood_view_t current_view = AUTH_FLOOD_VIEW_APS;
static wifi_ap_record_t selected_ap;
static bool is_running = false;
static uint32_t attempts_count = 0;
static lv_timer_t * attempts_timer = NULL;

extern lv_group_t * main_group;

static void list_event_cb(lv_event_t * e);

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
        loading_label = lv_label_create(screen_auth);
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

static void update_attack_labels(void) {
    if (lbl_target) {
        lv_label_set_text_fmt(lbl_target, "Target: %s  CH:%d", selected_ap.ssid, selected_ap.primary);
    }
    if (lbl_attempts) {
        lv_label_set_text_fmt(lbl_attempts, "Attempts: %lu", (unsigned long)attempts_count);
    }
    if (btn_attack) {
        lv_label_set_text(lv_obj_get_child(btn_attack, 0), is_running ? "FLOODING..." : "START FLOOD");
        lv_obj_set_style_bg_color(btn_attack, lv_color_hex(0x5A2CA0), 0);
    }
}

static void attempts_tick_cb(lv_timer_t * t) {
    (void)t;
    if (!is_running) return;
    attempts_count += 10;
    update_attack_labels();
}

static void stop_attack(void) {
    if (is_running) {
        wifi_flood_stop();
        is_running = false;
    }
    if (attempts_timer) {
        lv_timer_del(attempts_timer);
        attempts_timer = NULL;
    }
}

static void start_attack(void) {
    if (!wifi_flood_auth_start(selected_ap.bssid, selected_ap.primary)) {
        return;
    }

    is_running = true;
    if (attempts_timer) lv_timer_del(attempts_timer);
    attempts_timer = lv_timer_create(attempts_tick_cb, 200, NULL);
}

static void show_attack_view(void) {
    clear_list();
    clear_loading();
    current_view = AUTH_FLOOD_VIEW_ATTACK;
    is_running = false;
    attempts_count = 0;

    if (lbl_target) lv_obj_del(lbl_target);
    if (btn_attack) lv_obj_del(btn_attack);
    if (lbl_attempts) lv_obj_del(lbl_attempts);

    lbl_target = lv_label_create(screen_auth);
    lv_obj_set_style_text_color(lbl_target, current_theme.text_main, 0);
    lv_obj_align(lbl_target, LV_ALIGN_TOP_MID, 0, 30);

    btn_attack = button_ui_create(screen_auth, 170, 45, "START FLOOD");
    lv_obj_align(btn_attack, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_event_cb(btn_attack, list_event_cb, LV_EVENT_KEY, NULL);

    lbl_attempts = lv_label_create(screen_auth);
    lv_obj_set_style_text_color(lbl_attempts, current_theme.text_main, 0);
    lv_obj_align(lbl_attempts, LV_ALIGN_BOTTOM_MID, 0, -35);

    update_attack_labels();

    if (main_group) {
        lv_group_remove_all_objs(main_group);
        lv_group_add_obj(main_group, btn_attack);
        lv_group_focus_obj(btn_attack);
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
        if (current_view == AUTH_FLOOD_VIEW_ATTACK) {
            stop_attack();
            current_view = AUTH_FLOOD_VIEW_APS;
            if (lbl_target) { lv_obj_del(lbl_target); lbl_target = NULL; }
            if (btn_attack) { lv_obj_del(btn_attack); btn_attack = NULL; }
            if (lbl_attempts) { lv_obj_del(lbl_attempts); lbl_attempts = NULL; }
            clear_list();
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
            stop_attack();
            buzzer_play_sound_file("buzzer_click");
            ui_switch_screen(SCREEN_WIFI_ATTACK_MENU);
        }
    } else if (key == LV_KEY_ENTER) {
        if (current_view == AUTH_FLOOD_VIEW_APS) {
            lv_obj_t * focused = lv_group_get_focused(main_group);
            if (!focused) return;
            wifi_ap_record_t * ap = (wifi_ap_record_t *)lv_obj_get_user_data(focused);
            if (!ap) return;
            selected_ap = *ap;
            show_attack_view();
            buzzer_play_sound_file("buzzer_hacker_confirm");
        } else if (current_view == AUTH_FLOOD_VIEW_ATTACK) {
            if (!is_running) {
                start_attack();
                buzzer_play_sound_file("buzzer_hacker_confirm");
            } else {
                stop_attack();
                buzzer_play_sound_file("buzzer_click");
            }
            update_attack_labels();
        }
    }
}

void ui_wifi_auth_flood_open(void) {
    init_styles();
    if (screen_auth) lv_obj_del(screen_auth);

    screen_auth = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_auth, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_auth, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_auth);
    footer_ui_create(screen_auth);

    list_cont = lv_obj_create(screen_auth);
    lv_obj_set_size(list_cont, 230, 160);
    lv_obj_align(list_cont, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_style(list_cont, &style_menu, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
    lv_obj_add_event_cb(list_cont, list_event_cb, LV_EVENT_KEY, NULL);

    set_loading("SCANNING APS...");
    lv_screen_load(screen_auth);
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
}
