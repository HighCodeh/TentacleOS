#include "connect_wifi_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "keyboard_ui.h"
#include "msgbox_ui.h"
#include "buzzer.h"
#include "wifi_service.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "storage_assets.h"
#include "cJSON.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>

extern lv_group_t * main_group;

static lv_obj_t * screen_wifi_list = NULL;
static lv_obj_t * wifi_list_cont = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static char selected_ssid[33];
static char selected_pass[65];
static char known_pass[65];

static lv_timer_t * restore_group_timer = NULL;
static bool awaiting_connect = false;
static bool pending_connected = false;
static lv_timer_t * wifi_status_timer = NULL;
static uint32_t wifi_status_poll_count = 0;

static bool local_get_known_password(const char *ssid, char *password_buffer, size_t buffer_size) {
    if (!ssid || !password_buffer) return false;
    size_t size = 0;
    char *buffer = (char *)storage_assets_load_file(WIFI_KNOWN_NETWORKS_FILE, &size);
    if (!buffer) return false;

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    if (!root) return false;

    bool found = false;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        cJSON *j_ssid = cJSON_GetObjectItem(item, "ssid");
        if (cJSON_IsString(j_ssid) && strcmp(j_ssid->valuestring, ssid) == 0) {
            cJSON *j_pass = cJSON_GetObjectItem(item, "password");
            if (cJSON_IsString(j_pass)) {
                strncpy(password_buffer, j_pass->valuestring, buffer_size - 1);
                password_buffer[buffer_size - 1] = '\0';
                found = true;
            }
            break;
        }
    }
    cJSON_Delete(root);
    return found;
}

static void restore_wifi_group(lv_timer_t * timer) {
    (void)timer;
    restore_group_timer = NULL;
    if(!main_group || !wifi_list_cont) return;
    lv_group_remove_all_objs(main_group);
    uint32_t child_count = lv_obj_get_child_cnt(wifi_list_cont);
    for(uint32_t i = 0; i < child_count; i++) {
        lv_obj_t * child = lv_obj_get_child(wifi_list_cont, i);
        if(child) lv_group_add_obj(main_group, child);
    }
    lv_obj_t * first = lv_obj_get_child(wifi_list_cont, 0);
    if(first) lv_group_focus_obj(first);
}

static void on_msgbox_closed(bool confirm) {
    (void)confirm;
    if(restore_group_timer) lv_timer_del(restore_group_timer);
    restore_group_timer = lv_timer_create(restore_wifi_group, 10, NULL);
    lv_timer_set_repeat_count(restore_group_timer, 1);
}

static void on_wifi_status_async(void * user_data) {
    if(!awaiting_connect) return;
    awaiting_connect = false;
    msgbox_close();
    if(pending_connected) {
        msgbox_open(LV_SYMBOL_OK, "CONECTADO COM SUCESSO", "OK", NULL, on_msgbox_closed);
    } else {
        msgbox_open(LV_SYMBOL_CLOSE, "FALHA NA CONEXAO", "OK", NULL, on_msgbox_closed);
    }
}

static void wifi_status_timer_cb(lv_timer_t * timer) {
    if (!awaiting_connect) {
        lv_timer_del(timer);
        wifi_status_timer = NULL;
        return;
    }
    if (wifi_service_is_connected()) {
        pending_connected = true;
        lv_async_call(on_wifi_status_async, NULL);
        lv_timer_del(timer);
        wifi_status_timer = NULL;
        return;
    }
    if (++wifi_status_poll_count >= 20) {
        pending_connected = false;
        lv_async_call(on_wifi_status_async, NULL);
        lv_timer_del(timer);
        wifi_status_timer = NULL;
    }
}

static void on_keyboard_submit(const char * text, void * user_data) {
    strncpy(selected_pass, text ? text : "", sizeof(selected_pass) - 1);
    selected_pass[sizeof(selected_pass) - 1] = '\0';
    if(wifi_service_connect_to_ap(selected_ssid, selected_pass) == ESP_OK) {
        awaiting_connect = true;
        wifi_status_poll_count = 0;
        msgbox_open(LV_SYMBOL_WIFI, "CONECTANDO...", NULL, NULL, NULL);
        wifi_status_timer = lv_timer_create(wifi_status_timer_cb, 500, NULL);
    } else {
        msgbox_open(LV_SYMBOL_CLOSE, "FALHA NA CONEXAO", "OK", NULL, on_msgbox_closed);
    }
}

static void init_styles(void) {
    if(styles_initialized) return;
    
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

static void wifi_item_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * item = lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        lv_obj_set_style_border_color(item, current_theme.border_accent, 0);
        lv_obj_set_style_border_width(item, 2, 0);
        lv_obj_scroll_to_view(item, LV_ANIM_ON);
    } else if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_border_color(item, current_theme.border_inactive, 0);
        lv_obj_set_style_border_width(item, 1, 0);
    } else if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if(key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            ui_switch_screen(SCREEN_CONNECTION_SETTINGS);
        } else if(key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
            buzzer_play_sound_file("buzzer_hacker_confirm");
            wifi_ap_record_t * ap = (wifi_ap_record_t *)lv_obj_get_user_data(item);
            if(!ap) return;
            strncpy(selected_ssid, (const char *)ap->ssid, sizeof(selected_ssid) - 1);
            selected_ssid[sizeof(selected_ssid) - 1] = '\0';
            if(ap->authmode == WIFI_AUTH_OPEN) {
                on_keyboard_submit("", NULL);
            } else {
                if (local_get_known_password(selected_ssid, known_pass, sizeof(known_pass))) {
                    on_keyboard_submit(known_pass, NULL);
                } else {
                    keyboard_open(NULL, on_keyboard_submit, NULL);
                }
            }
        }
    }
}

void ui_connect_wifi_open(void) {
    init_styles();
    if(screen_wifi_list) lv_obj_del(screen_wifi_list);
    screen_wifi_list = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_wifi_list, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_wifi_list, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_wifi_list);
    footer_ui_create(screen_wifi_list);

    wifi_list_cont = lv_obj_create(screen_wifi_list);
    lv_obj_set_size(wifi_list_cont, 230, 160);
    lv_obj_align(wifi_list_cont, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_style(wifi_list_cont, &style_menu, 0);
    lv_obj_set_flex_flow(wifi_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(wifi_list_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(wifi_list_cont, LV_DIR_VER);

    lv_obj_t * loading = lv_label_create(screen_wifi_list);
    lv_label_set_text(loading, "SCANNING...");
    lv_obj_set_style_text_color(loading, current_theme.text_main, 0);
    lv_obj_center(loading);

    lv_screen_load(screen_wifi_list);
    lv_refr_now(NULL);

    if (!wifi_service_is_active()) {
        lv_label_set_text(loading, "WIFI OFF");
        return;
    }

    wifi_service_scan();
    uint16_t ap_count = wifi_service_get_ap_count();
    lv_obj_del(loading);

    if(main_group) lv_group_remove_all_objs(main_group);

    for(uint16_t i = 0; i < ap_count; i++) {
        wifi_ap_record_t * ap = wifi_service_get_ap_record(i);
        if(!ap) continue;

        lv_obj_t * item = lv_obj_create(wifi_list_cont);
        lv_obj_set_size(item, lv_pct(100), 40);
        lv_obj_add_style(item, &style_item, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * icon = lv_label_create(item);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

        lv_obj_t * lbl_ssid = lv_label_create(item);
        lv_label_set_text(lbl_ssid, (char *)ap->ssid);
        lv_obj_set_style_text_color(lbl_ssid, current_theme.text_main, 0);
        lv_obj_set_flex_grow(lbl_ssid, 1);
        lv_obj_set_style_margin_left(lbl_ssid, 8, 0);

        if(ap->authmode != WIFI_AUTH_OPEN) {
            lv_obj_t * lock = lv_label_create(item);
            lv_label_set_text(lock, "KEY");
            lv_obj_set_style_text_color(lock, current_theme.text_main, 0);
        }

        lv_obj_set_user_data(item, (void *)ap);
        lv_obj_add_event_cb(item, wifi_item_event_cb, LV_EVENT_ALL, NULL);
        if(main_group) lv_group_add_obj(main_group, item);
    }

    if(main_group) {
        lv_obj_t * first = lv_obj_get_child(wifi_list_cont, 0);
        if(first) lv_group_focus_obj(first);
    }
}