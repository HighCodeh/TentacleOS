#include "ui_theme.h"
#include "wifi_attack_menu_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buttons_gpio.h"
#include "esp_log.h"

static lv_obj_t * screen_wifi_attack_menu = NULL;
static menu_component_t menu;
static lv_timer_t * nav_timer = NULL;

static bool btn_up_last = false;
static bool btn_down_last = false;
static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_ok_last = false;
static bool btn_back_last = false;

static const struct {
    const char * name;
    const char * icon;
    int target;
} items[] = {
    {"DEAUTH ATTACK", NULL, SCREEN_WIFI_DEAUTH_ATTACK},
    {"BEACON SPAM",   NULL, SCREEN_WIFI_BEACON_SPAM_SIMPLE},
    {"PROBE FLOOD",   NULL, SCREEN_WIFI_PROBE_FLOOD},
    {"AUTH FLOOD",    NULL, SCREEN_WIFI_AUTH_FLOOD},
};

#define ITEM_COUNT (sizeof(items) / sizeof(items[0]))

static void nav_timer_cb(lv_timer_t * t) {
    if (lv_screen_active() != screen_wifi_attack_menu) {
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

    if (down && !btn_down_last) {
        menu_component_next(&menu);
    }
    if (up && !btn_up_last) {
        menu_component_prev(&menu);
    }
    if ((back && !btn_back_last) || (left && !btn_left_last)) {
        ui_switch_screen(SCREEN_WIFI_MENU);
    }
    if ((ok && !btn_ok_last) || (right && !btn_right_last)) {
        int sel = menu_component_get_selected(&menu);
        if (sel >= 0 && sel < (int)ITEM_COUNT) {
            ui_switch_screen(items[sel].target);
        }
    }

    btn_up_last    = up;
    btn_down_last  = down;
    btn_left_last  = left;
    btn_right_last = right;
    btn_ok_last    = ok;
    btn_back_last  = back;
}

void ui_wifi_attack_menu_open(void) {
    if (screen_wifi_attack_menu) { lv_obj_del(screen_wifi_attack_menu); screen_wifi_attack_menu = NULL; }

    screen_wifi_attack_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_wifi_attack_menu, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(screen_wifi_attack_menu, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_wifi_attack_menu, LV_OBJ_FLAG_SCROLLABLE);

    menu = menu_component_create(screen_wifi_attack_menu, "ATTACKS", "/assets/icons/wifi_menu_icon.bin");

    for (int i = 0; i < (int)ITEM_COUNT; i++) {
        menu_component_add_item(&menu, "/assets/icons/wifi_menu_icon.bin", items[i].name);
    }

    if (nav_timer == NULL) {
        nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    lv_screen_load(screen_wifi_attack_menu);
}
