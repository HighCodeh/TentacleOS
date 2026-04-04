// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ir_menu_ui.h"

#include "esp_log.h"

#include "ui_theme.h"
#include "ui_manager.h"
#include "menu_component_ui.h"
#include "buttons_gpio.h"

static const char *TAG = "IR_MENU_UI";

static lv_obj_t       *s_screen    = NULL;
static menu_component_t s_menu;
static lv_timer_t     *s_nav_timer = NULL;

static bool s_btn_up_last   = false;
static bool s_btn_down_last = false;
static bool s_btn_ok_last   = false;
static bool s_btn_back_last = false;

static const struct {
    const char *name;
    const char *icon;
    int         target;
} MENU_ITEMS[] = {
    {"Learn",          "/assets/icons/ir_receive_menu_icon.bin", SCREEN_IR_RECEIVE},
    {"Send",           "/assets/icons/ir_send_menu_icon.bin",    SCREEN_IR_SEND},
    {"Browse Signals", "/assets/icons/search_menu_icon.bin",     SCREEN_IR_SAVED},
    {"Burst",          "/assets/icons/burst_menu_icon.bin",      SCREEN_IR_BURST},
};
#define MENU_ITEMS_COUNT (sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))

static void nav_timer_cb(lv_timer_t *t) {
    if (lv_screen_active() != s_screen) {
        lv_timer_delete(t);
        s_nav_timer = NULL;
        return;
    }
    if (ui_input_is_locked()) return;

    bool up   = up_button_is_down();
    bool down = down_button_is_down();
    bool ok   = ok_button_is_down();
    bool back = back_button_is_down();

    if (down && !s_btn_down_last) menu_component_next(&s_menu);
    if (up && !s_btn_up_last)     menu_component_prev(&s_menu);

    if (back && !s_btn_back_last) {
        ui_switch_screen(SCREEN_MENU);
    }
    if (ok && !s_btn_ok_last) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel >= 0 && sel < (int)MENU_ITEMS_COUNT && MENU_ITEMS[sel].target >= 0) {
            ui_switch_screen(MENU_ITEMS[sel].target);
        }
    }

    s_btn_up_last   = up;
    s_btn_down_last = down;
    s_btn_ok_last   = ok;
    s_btn_back_last = back;
}

void ui_ir_menu_open(void) {
    if (s_screen != NULL) { lv_obj_del(s_screen); s_screen = NULL; }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_menu = menu_component_create(s_screen, "INFRARED", NULL);

    for (int i = 0; i < (int)MENU_ITEMS_COUNT; i++) {
        menu_component_add_item(&s_menu, MENU_ITEMS[i].icon, MENU_ITEMS[i].name);
    }

    if (s_nav_timer == NULL) {
        s_nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    lv_screen_load(s_screen);
}
