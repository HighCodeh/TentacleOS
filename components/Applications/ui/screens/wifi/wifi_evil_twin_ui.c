// Copyright (c) 2025 HIGH CODE LLC
#include "wifi_evil_twin_ui.h"
#include "ui_manager.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "evil_twin.h"
#include "wifi_service.h"
#include "buzzer.h"
#include "msgbox_ui.h"
#include "storage_assets.h"
#include "lvgl.h"
#include <dirent.h>
#include <string.h>
#include <stdio.h>

typedef enum {
    EVIL_TWIN_VIEW_APS = 0,
    EVIL_TWIN_VIEW_TEMPLATE = 1,
    EVIL_TWIN_VIEW_MONITOR = 2
} evil_twin_view_t;

typedef struct {
    char name[32];
    char path[96];
} template_item_t;

#define TEMPLATE_MAX 12
static template_item_t templates[TEMPLATE_MAX];
static int template_count = 0;

static lv_obj_t * screen_et = NULL;
static lv_obj_t * list_cont = NULL;
static lv_obj_t * loading_label = NULL;
static lv_obj_t * ta_term = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static evil_twin_view_t current_view = EVIL_TWIN_VIEW_APS;
static wifi_ap_record_t selected_ap;
static int selected_template = 0;
static lv_timer_t * monitor_timer = NULL;
static bool is_running = false;
static char last_password[64];

extern lv_group_t * main_group;

static void list_event_cb(lv_event_t * e);
static void populate_template_list(void);
static bool load_templates(void);

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
        loading_label = lv_label_create(screen_et);
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

static void update_monitor_terminal(void) {
    if (!ta_term) return;
    const char *status = "Aguardando vitima...";
    if (evil_twin_has_password()) {
        evil_twin_get_last_password(last_password, sizeof(last_password));
        status = "Vitima Conectada!";
    }
    char text[256];
    snprintf(text, sizeof(text),
             "Target: %s\n"
             "Template: %s\n"
             "%s\n"
             "Senha: %s\n",
             selected_ap.ssid,
             (template_count > 0) ? templates[selected_template].name : "-",
             status,
             evil_twin_has_password() ? last_password : "-");
    lv_textarea_set_text(ta_term, text);
}

static void stop_attack(void) {
    if (monitor_timer) {
        lv_timer_del(monitor_timer);
        monitor_timer = NULL;
    }
    if (is_running) {
        evil_twin_stop_attack();
        is_running = false;
    }
}

static void monitor_timer_cb(lv_timer_t * t) {
    (void)t;
    update_monitor_terminal();
}

static void show_monitor_view(void) {
    clear_list();
    clear_loading();
    current_view = EVIL_TWIN_VIEW_MONITOR;

    if (list_cont) lv_obj_add_flag(list_cont, LV_OBJ_FLAG_HIDDEN);
    if (ta_term) lv_obj_del(ta_term);

    if (!storage_assets_is_mounted()) {
        storage_assets_init();
    }
    size_t sz = 0;
    if (template_count == 0) {
        msgbox_open(LV_SYMBOL_CLOSE, "NO TEMPLATES FOUND", "OK", NULL, NULL);
        current_view = EVIL_TWIN_VIEW_TEMPLATE;
        if (list_cont) lv_obj_clear_flag(list_cont, LV_OBJ_FLAG_HIDDEN);
        populate_template_list();
        return;
    }
    char *probe = (char *)storage_assets_load_file(templates[selected_template].path, &sz);
    if (!probe) {
        msgbox_open(LV_SYMBOL_CLOSE, "TEMPLATE NOT FOUND", "OK", NULL, NULL);
        current_view = EVIL_TWIN_VIEW_TEMPLATE;
        if (list_cont) lv_obj_clear_flag(list_cont, LV_OBJ_FLAG_HIDDEN);
        populate_template_list();
        return;
    }
    free(probe);

    ta_term = lv_textarea_create(screen_et);
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

    evil_twin_reset_capture();
    memset(last_password, 0, sizeof(last_password));
    evil_twin_start_attack_with_template((const char *)selected_ap.ssid, templates[selected_template].path);
    is_running = true;

    update_monitor_terminal();
    if (monitor_timer) lv_timer_del(monitor_timer);
    monitor_timer = lv_timer_create(monitor_timer_cb, 500, NULL);

    lv_obj_add_event_cb(ta_term, list_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_et, list_event_cb, LV_EVENT_KEY, NULL);

    if (main_group) {
        lv_group_remove_all_objs(main_group);
        lv_group_add_obj(main_group, ta_term);
        lv_group_focus_obj(ta_term);
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

static void populate_template_list(void) {
    clear_list();
    if (template_count == 0) {
        lv_obj_t * empty = lv_label_create(list_cont);
        lv_label_set_text(empty, "NO TEMPLATES FOUND");
        lv_obj_set_style_text_color(empty, current_theme.text_main, 0);
        if (main_group) lv_group_add_obj(main_group, empty);
        return;
    }
    for (int i = 0; i < template_count; i++) {
        lv_obj_t * item = lv_obj_create(list_cont);
        lv_obj_set_size(item, lv_pct(100), 40);
        lv_obj_add_style(item, &style_item, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * icon = lv_label_create(item);
        lv_label_set_text(icon, LV_SYMBOL_FILE);
        lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

        lv_obj_t * lbl = lv_label_create(item);
        lv_label_set_text(lbl, templates[i].name);
        lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
        lv_obj_set_flex_grow(lbl, 1);
        lv_obj_set_style_margin_left(lbl, 8, 0);

        lv_obj_set_user_data(item, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(item, item_focus_cb, LV_EVENT_ALL, NULL);
        if (main_group) lv_group_add_obj(main_group, item);
    }

    if (main_group) {
        lv_obj_t * first = lv_obj_get_child(list_cont, 0);
        if (first) lv_group_focus_obj(first);
    }
}

static bool load_templates(void) {
    template_count = 0;
    if (!storage_assets_is_mounted()) {
        storage_assets_init();
    }
    const char *base = storage_assets_get_mount_point();
    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s/html/captive_portal", base);

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len < 6) continue;
        if (strcmp(name + len - 5, ".html") != 0) continue;
        if (template_count >= TEMPLATE_MAX) break;

        template_item_t *t = &templates[template_count++];
        const char *prefix = "html/captive_portal/";
        int max_name = (int)sizeof(t->path) - (int)strlen(prefix) - 1;
        if (max_name <= 0) {
            continue;
        }
        snprintf(t->path, sizeof(t->path), "%s%.*s", prefix, max_name, name);
        size_t base_len = len - 5;
        if (base_len >= sizeof(t->name)) base_len = sizeof(t->name) - 1;
        memcpy(t->name, name, base_len);
        t->name[base_len] = '\0';
    }

    closedir(dir);
    return template_count > 0;
}

static void list_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        if (current_view == EVIL_TWIN_VIEW_MONITOR) {
            stop_attack();
            if (ta_term) { lv_obj_del(ta_term); ta_term = NULL; }
            if (list_cont) lv_obj_clear_flag(list_cont, LV_OBJ_FLAG_HIDDEN);
            current_view = EVIL_TWIN_VIEW_TEMPLATE;
            populate_template_list();
            return;
        }
        if (current_view == EVIL_TWIN_VIEW_TEMPLATE) {
            current_view = EVIL_TWIN_VIEW_APS;
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
            return;
        }
        buzzer_play_sound_file("buzzer_click");
        ui_switch_screen(SCREEN_WIFI_MENU);
        return;
    }

    if (key == LV_KEY_ENTER) {
        if (current_view == EVIL_TWIN_VIEW_APS) {
            lv_obj_t * focused = lv_group_get_focused(main_group);
            if (!focused) return;
            wifi_ap_record_t * ap = (wifi_ap_record_t *)lv_obj_get_user_data(focused);
            if (!ap) return;
            selected_ap = *ap;
            current_view = EVIL_TWIN_VIEW_TEMPLATE;
            load_templates();
            populate_template_list();
            buzzer_play_sound_file("buzzer_hacker_confirm");
        } else if (current_view == EVIL_TWIN_VIEW_TEMPLATE) {
            lv_obj_t * focused = lv_group_get_focused(main_group);
            if (!focused) return;
            selected_template = (int)(uintptr_t)lv_obj_get_user_data(focused);
            show_monitor_view();
            buzzer_play_sound_file("buzzer_hacker_confirm");
        }
    }
}

void ui_wifi_evil_twin_open(void) {
    init_styles();
    if (screen_et) lv_obj_del(screen_et);

    screen_et = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_et, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_et, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_et);
    footer_ui_create(screen_et);

    list_cont = lv_obj_create(screen_et);
    lv_obj_set_size(list_cont, 230, 160);
    lv_obj_align(list_cont, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_style(list_cont, &style_menu, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);
    lv_obj_add_event_cb(list_cont, list_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(screen_et, list_event_cb, LV_EVENT_KEY, NULL);

    set_loading("SCANNING APS...");
    lv_screen_load(screen_et);
    lv_refr_now(NULL);

    current_view = EVIL_TWIN_VIEW_APS;
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

void ui_wifi_evil_twin_set_ssid(const char *ssid) {
    (void)ssid;
}
