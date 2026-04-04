#include "ui_theme.h"
#include "home_ui.h"
#include "header_ui.h"
#include "dropdown_ui.h"

#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "assets_manager.h"
#include "st7789.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>

#define BG_COLOR current_theme.screen_base
#define HEADER_HEIGHT_HOME ((LCD_V_RES * 9) / 100)
static const char *TAG = "HOME_UI";

static lv_obj_t * screen_home = NULL;

static void home_event_cb(lv_event_t * e)
{
    if (dropdown_ui_is_open()) return;
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if(key == LV_KEY_RIGHT) {
            ui_switch_screen(SCREEN_MENU);
        }
    }
}

void ui_home_open(void)
{

    if (screen_home) { lv_obj_del(screen_home); screen_home = NULL; }

    screen_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_home, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(screen_home, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_home, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_home);
    dropdown_ui_create(screen_home);

    static lv_image_dsc_t * push_dsc = NULL;
    if (!push_dsc) push_dsc = assets_get("/assets/icons/push_icon.bin");

    static lv_obj_t * push_icons[4];

    if (push_dsc) {
        int16_t w = push_dsc->header.w;
        int16_t h = push_dsc->header.h;

        push_icons[0] = lv_image_create(screen_home);
        lv_image_set_src(push_icons[0], push_dsc);
        lv_obj_align(push_icons[0], LV_ALIGN_TOP_MID, 0, HEADER_HEIGHT_HOME);

        push_icons[1] = lv_image_create(screen_home);
        lv_image_set_src(push_icons[1], push_dsc);
        lv_image_set_pivot(push_icons[1], w / 2, h / 2);
        lv_image_set_rotation(push_icons[1], 1800);
        lv_obj_align(push_icons[1], LV_ALIGN_BOTTOM_MID, 0, 0);

        push_icons[2] = lv_image_create(screen_home);
        lv_image_set_src(push_icons[2], push_dsc);
        lv_image_set_pivot(push_icons[2], w / 2, h / 2);
        lv_image_set_rotation(push_icons[2], 2700);
        lv_obj_align(push_icons[2], LV_ALIGN_LEFT_MID, -h / 2, 0);

        push_icons[3] = lv_image_create(screen_home);
        lv_image_set_src(push_icons[3], push_dsc);
        lv_image_set_pivot(push_icons[3], w / 2, h / 2);
        lv_image_set_rotation(push_icons[3], 900);
        lv_obj_align(push_icons[3], LV_ALIGN_RIGHT_MID, h / 2, 0);

        dropdown_ui_register_hide_objs(push_icons, 4);
    }

    lv_obj_add_event_cb(screen_home, home_event_cb, LV_EVENT_KEY, NULL);
    
    if(main_group) {
        lv_group_add_obj(main_group, screen_home);
        lv_group_focus_obj(screen_home);
    }

    lv_screen_load(screen_home);
}