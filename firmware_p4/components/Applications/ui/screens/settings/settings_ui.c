#include "ui_theme.h"
#include "settings_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buttons_gpio.h"
#include "esp_log.h"

static lv_obj_t * screen_settings = NULL;
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
    {"CONNECTION", "/assets/icons/wifi_menu_icon.bin", SCREEN_CONNECTION_SETTINGS},
    {"INTERFACE",  "/assets/icons/interface_menu_icon.bin", SCREEN_INTERFACE_SETTINGS},
    {"DISPLAY",    "/assets/icons/display_menu_icon.bin", SCREEN_DISPLAY_SETTINGS},
    {"SOUND",      NULL, SCREEN_SOUND_SETTINGS},
    {"BATTERY",    "/assets/icons/battery_menu_icon.bin", SCREEN_BATTERY_SETTINGS},
    {"ABOUT",      "/assets/icons/about_menu_icon.bin", SCREEN_ABOUT_SETTINGS},
};

#define ITEM_COUNT (sizeof(items) / sizeof(items[0]))

static void nav_timer_cb(lv_timer_t * t) {
    if (lv_screen_active() != screen_settings) {
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
        ui_switch_screen(SCREEN_MENU);
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

void ui_settings_open(void) {
    if (screen_settings) { lv_obj_del(screen_settings); screen_settings = NULL; }

    screen_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_settings, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(screen_settings, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_settings, LV_OBJ_FLAG_SCROLLABLE);

    menu = menu_component_create(screen_settings, "SETTINGS", NULL);

    for (int i = 0; i < (int)ITEM_COUNT; i++) {
        menu_component_add_item(&menu, items[i].icon, items[i].name);
    }

    if (nav_timer == NULL) {
        nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    lv_screen_load(screen_settings);
}
