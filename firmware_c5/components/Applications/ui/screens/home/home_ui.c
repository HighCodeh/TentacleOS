#include "home_ui.h"
#include "header_ui.h"
#include "footer_ui.h"

#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "assets_manager.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>

#define BG_COLOR lv_color_black()
static const char *TAG = "HOME_UI";

static lv_obj_t * screen_home = NULL;
static lv_image_dsc_t * octobit_img_dsc = NULL;

static void home_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if(key == LV_KEY_LEFT) {
            ui_switch_screen(SCREEN_MENU);
        }
    }
}

void ui_home_open(void)
{

    if(screen_home) {
        lv_obj_del(screen_home);
        screen_home = NULL;
    }

    // Create new screen
    screen_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_home, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(screen_home, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_home, LV_OBJ_FLAG_SCROLLABLE);

    if(octobit_img_dsc == NULL) {
        octobit_img_dsc = assets_get("/assets/img/OCTOBIT.bin");
    }

    if(octobit_img_dsc) {
        lv_obj_t * img_obj = lv_image_create(screen_home);
        lv_image_set_src(img_obj, octobit_img_dsc);
        
        lv_obj_center(img_obj);
        
        lv_obj_set_style_opa(img_obj, LV_OPA_COVER, 0);
    } else {
        ESP_LOGE(TAG, "Failed to display OCTOBIT (DSC null)");
    }

    header_ui_create(screen_home);
    footer_ui_create(screen_home);

    lv_obj_add_event_cb(screen_home, home_event_cb, LV_EVENT_KEY, NULL);
    
    if(main_group) {
        lv_group_add_obj(main_group, screen_home);
        lv_group_focus_obj(screen_home);
    }

    lv_screen_load(screen_home);
}