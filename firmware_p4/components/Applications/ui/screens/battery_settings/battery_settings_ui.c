#include "ui_theme.h"
#include "battery_settings_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buttons_gpio.h"
#include "esp_log.h"

static lv_obj_t * screen_battery = NULL;
static menu_component_t menu;
static lv_timer_t * nav_timer = NULL;

static bool btn_up_last = false;
static bool btn_down_last = false;
static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_ok_last = false;
static bool btn_back_last = false;

/* Item indices */
#define IDX_PWR_SAVE  0
#define IDX_TIMEOUT   1
#define IDX_MODE      2

static bool power_save = false;
static int timeout_idx = 1;
static const char * timeout_options[] = {"30s", "1m", "5m", "NEVER"};
#define TIMEOUT_COUNT 4

static int perf_idx = 1;
static const char * perf_options[] = {"MIN", "BAL", "MAX"};
#define PERF_COUNT 3

static void nav_timer_cb(lv_timer_t * t) {
    if (lv_screen_active() != screen_battery) {
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

    /* LEFT / RIGHT change selector values */
    if (right && !btn_right_last) {
        if (sel == IDX_PWR_SAVE) {
            menu_component_toggle_item(&menu, IDX_PWR_SAVE);
            power_save = menu_component_get_toggle(&menu, IDX_PWR_SAVE);
        } else if (sel == IDX_TIMEOUT) {
            timeout_idx = (timeout_idx + 1) % TIMEOUT_COUNT;
            menu_component_set_selector_value(&menu, IDX_TIMEOUT, timeout_options[timeout_idx]);
        } else if (sel == IDX_MODE) {
            perf_idx = (perf_idx + 1) % PERF_COUNT;
            menu_component_set_selector_value(&menu, IDX_MODE, perf_options[perf_idx]);
        }
    }

    if (left && !btn_left_last) {
        if (sel == IDX_PWR_SAVE) {
            menu_component_toggle_item(&menu, IDX_PWR_SAVE);
            power_save = menu_component_get_toggle(&menu, IDX_PWR_SAVE);
        } else if (sel == IDX_TIMEOUT) {
            timeout_idx = (timeout_idx - 1 + TIMEOUT_COUNT) % TIMEOUT_COUNT;
            menu_component_set_selector_value(&menu, IDX_TIMEOUT, timeout_options[timeout_idx]);
        } else if (sel == IDX_MODE) {
            perf_idx = (perf_idx - 1 + PERF_COUNT) % PERF_COUNT;
            menu_component_set_selector_value(&menu, IDX_MODE, perf_options[perf_idx]);
        }
    }

    btn_up_last    = up;
    btn_down_last  = down;
    btn_left_last  = left;
    btn_right_last = right;
    btn_ok_last    = ok;
    btn_back_last  = back;
}

void ui_battery_settings_open(void) {
    if (screen_battery) { lv_obj_del(screen_battery); screen_battery = NULL; }

    screen_battery = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_battery, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(screen_battery, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_battery, LV_OBJ_FLAG_SCROLLABLE);

    menu = menu_component_create(screen_battery, "BATTERY", NULL);

    menu_component_add_toggle(&menu, "/assets/icons/battery_menu_icon.bin",
                              "PWR SAVE", power_save);
    menu_component_add_selector(&menu, NULL,
                                "TIMEOUT", timeout_options[timeout_idx]);
    menu_component_add_selector(&menu, NULL,
                                "MODE", perf_options[perf_idx]);

    if (nav_timer == NULL) {
        nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    lv_screen_load(screen_battery);
}
