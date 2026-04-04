#include "ui_theme.h"
#include "display_settings_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "buttons_gpio.h"
#include "esp_log.h"
#include <stdio.h>
#include "st7789.h"

static lv_obj_t * screen_display = NULL;
static menu_component_t menu;
static lv_timer_t * nav_timer = NULL;

static bool btn_up_last = false;
static bool btn_down_last = false;
static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_ok_last = false;
static bool btn_back_last = false;

static int brightness_val = 3;
static int rotation_val = 1;

enum {
    ITEM_BRIGHTNESS = 0,
    ITEM_ROTATION   = 1,
};

void update_lvgl_display_rotation(uint8_t rotation) {
    lv_obj_invalidate(lv_scr_act());
}

static void update_rotation_value(void) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", rotation_val);
    menu_component_set_selector_value(&menu, ITEM_ROTATION, buf);
}

static void nav_timer_cb(lv_timer_t * t) {
    if (lv_screen_active() != screen_display) {
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
    if (back && !btn_back_last) {
        ui_switch_screen(SCREEN_SETTINGS);
    }

    int sel = menu_component_get_selected(&menu);

    if (left && !btn_left_last) {
        if (sel == ITEM_BRIGHTNESS) {
            menu_component_intensity_dec(&menu, ITEM_BRIGHTNESS);
            brightness_val = menu_component_get_intensity(&menu, ITEM_BRIGHTNESS);
            lcd_set_brightness(brightness_val * 20);
        } else if (sel == ITEM_ROTATION) {
            rotation_val = (rotation_val == 1) ? 4 : rotation_val - 1;
            lcd_set_rotation(rotation_val);
            update_lvgl_display_rotation(rotation_val);
            update_rotation_value();
        }
    }
    if (right && !btn_right_last) {
        if (sel == ITEM_BRIGHTNESS) {
            menu_component_intensity_inc(&menu, ITEM_BRIGHTNESS);
            brightness_val = menu_component_get_intensity(&menu, ITEM_BRIGHTNESS);
            lcd_set_brightness(brightness_val * 20);
        } else if (sel == ITEM_ROTATION) {
            rotation_val = (rotation_val % 4) + 1;
            lcd_set_rotation(rotation_val);
            update_lvgl_display_rotation(rotation_val);
            update_rotation_value();
        }
    }

    btn_up_last    = up;
    btn_down_last  = down;
    btn_left_last  = left;
    btn_right_last = right;
    btn_ok_last    = ok;
    btn_back_last  = back;
}

void ui_display_settings_open(void) {
    if (screen_display) { lv_obj_del(screen_display); screen_display = NULL; }

    brightness_val = lcd_get_brightness() / 20;
    if (brightness_val < 1) brightness_val = 1;
    rotation_val = lcd_get_rotation();

    screen_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_display, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(screen_display, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_display, LV_OBJ_FLAG_SCROLLABLE);

    menu = menu_component_create(screen_display, "DISPLAY", NULL);

    menu_component_add_intensity(&menu, "/assets/icons/bright_menu_icon.bin", "BRIGHTNESS", brightness_val);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", rotation_val);
    menu_component_add_selector(&menu, "/assets/icons/rotate_menu_icon.bin", "ROTATION", buf);

    if (nav_timer == NULL) {
        nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    lv_screen_load(screen_display);
}
