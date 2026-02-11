#include "connection_settings_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buzzer.h"
#include "esp_log.h"
#include "wifi_service.h"
#include "msgbox_ui.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "CONN_UI";

static lv_obj_t * screen_conn = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;
static lv_obj_t * item_wifi_sw = NULL;
static lv_obj_t * item_wifi_nav = NULL;
static int64_t msgbox_open_time = 0;
static lv_timer_t * wifi_loading_timer = NULL;
static int64_t wifi_loading_start_time = 0;

static void init_styles(void) {
    if(styles_initialized) {
        lv_style_reset(&style_menu);
        lv_style_reset(&style_item);
    }

    lv_style_init(&style_menu);
    lv_style_set_bg_opa(&style_menu, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_menu, 2);
    lv_style_set_border_color(&style_menu, current_theme.border_accent);
    lv_style_set_radius(&style_menu, 2);
    lv_style_set_pad_all(&style_menu, 8);
    lv_style_set_pad_row(&style_menu, 6);

    lv_style_init(&style_item);
    lv_style_set_bg_color(&style_item, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_item, current_theme.bg_item_top);
    lv_style_set_bg_grad_dir(&style_item, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_item, 1);
    lv_style_set_border_color(&style_item, current_theme.border_inactive);
    lv_style_set_radius(&style_item, 0);
    
    styles_initialized = true;
}

static void update_switch_visual(lv_obj_t * cont, bool state) {
    if(!cont) return;
    lv_obj_t * off_ind = lv_obj_get_child(cont, 0);
    lv_obj_t * on_ind = lv_obj_get_child(cont, 1);
    
    lv_obj_set_style_bg_color(off_ind, current_theme.text_main, 0); 
    lv_obj_set_style_bg_color(on_ind, current_theme.text_main, 0);
    
    lv_obj_set_style_bg_opa(off_ind, !state ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(on_ind, state ? LV_OPA_COVER : LV_OPA_30, 0);
}

static void restore_connection_group(void) {
    if (!main_group || !item_wifi_sw || !item_wifi_nav) return;
    lv_group_set_editing(main_group, false);
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, item_wifi_sw);
    lv_group_add_obj(main_group, item_wifi_nav);
    lv_group_focus_obj(item_wifi_sw);
}

static void restore_connection_group_async(void * user_data) {
    (void)user_data;
    restore_connection_group();
}

static void on_wifi_off_msgbox_closed(bool confirm) {
    (void)confirm;
    lv_async_call(restore_connection_group_async, NULL);
}

static void wifi_loading_timer_cb(lv_timer_t * timer) {
    int64_t elapsed = esp_timer_get_time() - wifi_loading_start_time;
    if ((wifi_service_is_active() && elapsed >= 1500000) || elapsed >= 5000000) {
        lv_timer_del(timer);
        wifi_loading_timer = NULL;
        msgbox_close();
        restore_connection_group();
    }
}

static void show_wifi_loading(void) {
    if (wifi_loading_timer) lv_timer_del(wifi_loading_timer);
    wifi_loading_start_time = esp_timer_get_time();
    msgbox_open(LV_SYMBOL_WIFI, "LIGANDO WIFI...", NULL, NULL, NULL);
    wifi_loading_timer = lv_timer_create(wifi_loading_timer_cb, 100, NULL);
}

static void conn_item_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * item = lv_event_get_target(e);
    int type = (int)(uintptr_t)lv_event_get_user_data(e);

    if(code == LV_EVENT_FOCUSED) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        lv_obj_set_style_border_color(item, current_theme.border_accent, 0);
        lv_obj_set_style_border_width(item, 2, 0);
        lv_obj_scroll_to_view(item, LV_ANIM_ON);
    } 
    else if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_border_color(item, current_theme.border_inactive, 0);
        lv_obj_set_style_border_width(item, 1, 0);
    }
    else if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);

        if(key == LV_KEY_ESC) {
            buzzer_play_sound_file("buzzer_click");
            ui_switch_screen(SCREEN_SETTINGS);
            return;
        }

        if(key == LV_KEY_ENTER || key == LV_KEY_RIGHT || key == LV_KEY_LEFT) {
            if(type == 0) {
                bool current_state = wifi_service_is_active();
                bool new_state = !current_state;
                update_switch_visual(lv_obj_get_child(item, 2), new_state);
                wifi_set_wifi_enabled(new_state); 
                
                if(new_state) {
                    show_wifi_loading();
                } else {
                    msgbox_close();
                }
                
                buzzer_play_sound_file("buzzer_hacker_confirm");
            }
            else if(type == 1) {
                if (!wifi_service_is_active()) {
                    int64_t now = esp_timer_get_time();
                    if (now - msgbox_open_time < 300000) return;
                    msgbox_open_time = now;
                    msgbox_open(LV_SYMBOL_CLOSE, "WIFI OFF", "OK", NULL, on_wifi_off_msgbox_closed);
                } else {
                    buzzer_play_sound_file("buzzer_hacker_confirm");
                    ui_switch_screen(SCREEN_CONNECT_WIFI);
                }
            }
        }
    }
}

static lv_obj_t * create_menu_item(lv_obj_t * parent, const char * symbol, const char * name) {
    lv_obj_t * item = lv_obj_create(parent);
    lv_obj_set_size(item, lv_pct(100), 40);
    lv_obj_add_style(item, &style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * icon = lv_label_create(item);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

    lv_obj_t * label = lv_label_create(item);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, current_theme.text_main, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_margin_left(label, 8, 0);

    return item;
}

static lv_obj_t * create_switch_input(lv_obj_t * parent, bool initial_state) {
    lv_obj_t * sw_cont = lv_obj_create(parent);
    lv_obj_set_size(sw_cont, 45, 24);
    lv_obj_set_style_bg_opa(sw_cont, 0, 0);
    lv_obj_set_style_border_width(sw_cont, 1, 0);
    lv_obj_set_style_border_color(sw_cont, current_theme.border_inactive, 0);
    lv_obj_set_flex_flow(sw_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sw_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(sw_cont, 4, 0);
    lv_obj_clear_flag(sw_cont, LV_OBJ_FLAG_SCROLLABLE);

    for(int i = 0; i < 2; i++) {
        lv_obj_t * b = lv_obj_create(sw_cont);
        lv_obj_set_size(b, 14, 16);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    }
    update_switch_visual(sw_cont, initial_state);
    return sw_cont;
}

void ui_connection_settings_open(void) {
    init_styles();
    if(screen_conn) lv_obj_del(screen_conn);

    bool wifi_active = wifi_service_is_active();

    screen_conn = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_conn, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_conn, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_conn);
    footer_ui_create(screen_conn);

    lv_obj_t * menu = lv_obj_create(screen_conn);
    lv_obj_set_size(menu, 230, 170);
    lv_obj_align(menu, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_style(menu, &style_menu, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);

    item_wifi_sw = create_menu_item(menu, LV_SYMBOL_WIFI, "WI-FI");
    create_switch_input(item_wifi_sw, wifi_active);
    lv_obj_add_event_cb(item_wifi_sw, conn_item_event_cb, LV_EVENT_ALL, (void*)0);

    item_wifi_nav = create_menu_item(menu, LV_SYMBOL_SETTINGS, "NETWORKS"); 
    lv_obj_t * icon_next = lv_label_create(item_wifi_nav);
    lv_label_set_text(icon_next, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(icon_next, current_theme.text_main, 0); 
    lv_obj_add_event_cb(item_wifi_nav, conn_item_event_cb, LV_EVENT_ALL, (void*)1);

    if(main_group) {
        restore_connection_group();
    }

    lv_screen_load(screen_conn);
}