#include "ui_theme.h"
#include "sound_settings_ui.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "buttons_gpio.h"

static lv_obj_t * screen_sound = NULL;
static menu_component_t menu;
static lv_timer_t * nav_timer = NULL;

static bool btn_up_last = false;
static bool btn_down_last = false;
static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_back_last = false;

static int volume_val = 3;

enum {
    ITEM_VOLUME = 0,
    ITEM_BUZZER = 1,
};

static void nav_timer_cb(lv_timer_t * t) {
    if (lv_screen_active() != screen_sound) {
        lv_timer_delete(t);
        nav_timer = NULL;
        return;
    }
    if (ui_input_is_locked()) return;
    bool up    = up_button_is_down();
    bool down  = down_button_is_down();
    bool left  = left_button_is_down();
    bool right = right_button_is_down();
    bool back  = back_button_is_down();

    if (down && !btn_down_last) menu_component_next(&menu);
    if (up && !btn_up_last) menu_component_prev(&menu);
    if (back && !btn_back_last) { ui_switch_screen(SCREEN_SETTINGS); return; }

    int sel = menu_component_get_selected(&menu);

    if (left && !btn_left_last) {
        if (sel == ITEM_VOLUME) {
            menu_component_intensity_dec(&menu, ITEM_VOLUME);
            volume_val = menu_component_get_intensity(&menu, ITEM_VOLUME);
        } else if (sel == ITEM_BUZZER) {
            menu_component_toggle_item(&menu, ITEM_BUZZER);
        }
    }
    if (right && !btn_right_last) {
        if (sel == ITEM_VOLUME) {
            menu_component_intensity_inc(&menu, ITEM_VOLUME);
            volume_val = menu_component_get_intensity(&menu, ITEM_VOLUME);
        } else if (sel == ITEM_BUZZER) {
            menu_component_toggle_item(&menu, ITEM_BUZZER);
        }
    }

    btn_up_last    = up;
    btn_down_last  = down;
    btn_left_last  = left;
    btn_right_last = right;
    btn_back_last  = back;
}

void ui_sound_settings_open(void) {
    if (screen_sound) { lv_obj_del(screen_sound); screen_sound = NULL; }

    screen_sound = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_sound, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(screen_sound, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_sound, LV_OBJ_FLAG_SCROLLABLE);

    menu = menu_component_create(screen_sound, "SOUND", NULL);

    menu_component_add_intensity(&menu, NULL, "VOLUME", volume_val);
    menu_component_add_toggle(&menu, NULL, "BUZZER", false);

    if (nav_timer == NULL)
        nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);

    lv_screen_load(screen_sound);
}
