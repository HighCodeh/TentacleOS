#include "ui_theme.h"
#include "connection_settings_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buttons_gpio.h"
#include "esp_log.h"
#include "wifi_service.h"
#include "msgbox_ui.h"
#include "esp_timer.h"

static const char *TAG = "CONN_UI";

static lv_obj_t * screen_conn = NULL;
static menu_component_t menu;
static lv_timer_t * nav_timer = NULL;
static lv_timer_t * wifi_loading_timer = NULL;
static int64_t wifi_loading_start_time = 0;
static int64_t msgbox_open_time = 0;

static bool btn_up_last = false;
static bool btn_down_last = false;
static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_ok_last = false;
static bool btn_back_last = false;

/* Item indices */
#define IDX_WIFI      0
#define IDX_NETWORKS  1

static void wifi_loading_timer_cb(lv_timer_t * timer) {
    int64_t elapsed = esp_timer_get_time() - wifi_loading_start_time;
    if ((wifi_service_is_active() && elapsed >= 1500000) || elapsed >= 5000000) {
        lv_timer_del(timer);
        wifi_loading_timer = NULL;
        msgbox_close();
    }
}

static void show_wifi_loading(void) {
    if (wifi_loading_timer) lv_timer_del(wifi_loading_timer);
    wifi_loading_start_time = esp_timer_get_time();
    msgbox_open(LV_SYMBOL_WIFI, "LIGANDO WIFI...", NULL, NULL, NULL);
    wifi_loading_timer = lv_timer_create(wifi_loading_timer_cb, 100, NULL);
}

static void update_wifi_toggle(void) {
    bool active = wifi_service_is_active();
    menu_component_set_toggle(&menu, IDX_WIFI, active);
}

static void nav_timer_cb(lv_timer_t * t) {
    if (lv_screen_active() != screen_conn) {
        lv_timer_delete(t);
        nav_timer = NULL;
        return;
    }
    if (ui_input_is_locked()) return;
    bool up    = up_button_is_down();
    bool down  = down_button_is_down();
    bool left  = left_button_is_down();
    bool right = right_button_is_down();
    bool ok    = ok_button_is_down();
    bool back  = back_button_is_down();

    int sel = menu_component_get_selected(&menu);

    if (down && !btn_down_last) {
        menu_component_next(&menu);
    }
    if (up && !btn_up_last) {
        menu_component_prev(&menu);
    }

    if (back && !btn_back_last) {
        ui_switch_screen(SCREEN_SETTINGS);
    }

    /* LEFT / RIGHT toggle wifi or ignored on nav items */
    if ((left && !btn_left_last) || (right && !btn_right_last)) {
        if (sel == IDX_WIFI) {
            menu_component_toggle_item(&menu, IDX_WIFI);
            bool new_state = menu_component_get_toggle(&menu, IDX_WIFI);
            wifi_set_wifi_enabled(new_state);

            if (new_state) {
                show_wifi_loading();
            } else {
                msgbox_close();
            }

        }
    }

    /* OK or RIGHT on NETWORKS item navigates */
    if ((ok && !btn_ok_last) || (right && !btn_right_last)) {
        if (sel == IDX_NETWORKS) {
            if (!wifi_service_is_active()) {
                int64_t now = esp_timer_get_time();
                if (now - msgbox_open_time >= 300000) {
                    msgbox_open_time = now;
                    msgbox_open(LV_SYMBOL_CLOSE, "WIFI OFF", "OK", NULL, NULL);
                }
            } else {
                ui_switch_screen(SCREEN_CONNECT_WIFI);
            }
        }
    }

    btn_up_last    = up;
    btn_down_last  = down;
    btn_left_last  = left;
    btn_right_last = right;
    btn_ok_last    = ok;
    btn_back_last  = back;
}

void ui_connection_settings_open(void) {
    if (screen_conn) { lv_obj_del(screen_conn); screen_conn = NULL; }

    bool wifi_active = wifi_service_is_active();

    screen_conn = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_conn, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(screen_conn, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_conn, LV_OBJ_FLAG_SCROLLABLE);

    menu = menu_component_create(screen_conn, "CONNECTION", NULL);

    menu_component_add_toggle(&menu, "/assets/icons/wifi_menu_icon.bin",
                              "WI-FI", wifi_active);
    menu_component_add_item(&menu, "/assets/icons/search_menu_icon.bin", "NETWORKS");

    if (nav_timer == NULL) {
        nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    lv_screen_load(screen_conn);
}
