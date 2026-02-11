#include "wifi_ui.h"
#include "font/lv_symbol_def.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "esp_log.h"
#include "buzzer.h"

static lv_obj_t * screen_wifi_menu = NULL;
static lv_style_t style_menu;
static lv_style_t style_btn;
static bool styles_initialized = false;

static void wifi_menu_event_cb(lv_event_t * e);
static const char *TAG = "UI_WIFI_MENU";

static void init_styles(void)
{
    if(styles_initialized) {
        lv_style_reset(&style_menu);
        lv_style_reset(&style_btn);
    }
    
    lv_style_init(&style_menu);
    lv_style_set_bg_opa(&style_menu, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_menu, 2);
    lv_style_set_border_color(&style_menu, current_theme.border_accent);
    lv_style_set_radius(&style_menu, 6);
    lv_style_set_pad_all(&style_menu, 10);
    lv_style_set_pad_row(&style_menu, 10);
    
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_btn, current_theme.bg_item_top);
    lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_color(&style_btn, current_theme.border_inactive);
    lv_style_set_radius(&style_btn, 4);
    
    styles_initialized = true;
}

static void menu_item_event_cb(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * img_sel = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_FOCUSED) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        lv_obj_clear_flag(img_sel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(btn, current_theme.border_accent, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
    }
    else if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(img_sel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(btn, current_theme.border_inactive, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
    }
}

static void wifi_option_select_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    
    if (key == LV_KEY_ENTER) {
        int index = (int)(intptr_t)lv_event_get_user_data(e);
        buzzer_play_sound_file("buzzer_hacker_confirm");
        
        switch(index) {
            case 0: ui_switch_screen(SCREEN_WIFI_SCAN_MENU); break;
            case 1: ui_switch_screen(SCREEN_WIFI_DEAUTH); break;
            case 2: ui_switch_screen(SCREEN_WIFI_EVIL_TWIN); break;
            case 3: ui_switch_screen(SCREEN_WIFI_BEACON_SPAM); break;
            case 4: ui_switch_screen(SCREEN_WIFI_PROBE); break;
            case 5: /* Port Scan - TODO */ break;
            default: break;
        }
    }
}

static void create_wifi_menu(lv_obj_t * parent)
{
    init_styles();
    
    lv_coord_t menu_h = 240 - 24 - 20;

    lv_obj_t * menu = lv_obj_create(parent);
    lv_obj_set_size(menu, 230, menu_h);
    lv_obj_align(menu, LV_ALIGN_CENTER, 0, 5);
    lv_obj_add_style(menu, &style_menu, 0);
    lv_obj_set_scroll_dir(menu, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);

    static const char * options[] = {
        "Scan Networks", 
        "Deauth Packets",
        "Evil-Twin", 
        "Beacon Spam",
        "Probe Monitor",
        "Port Scan"
    };
    
    for(int i = 0; i < 6; i++) {
        lv_obj_t * btn = lv_btn_create(menu);
        lv_obj_set_size(btn, lv_pct(100), 44);
        lv_obj_add_style(btn, &style_btn, 0);

        lv_obj_t * img_left = lv_label_create(btn);
        lv_label_set_text(img_left, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(img_left, current_theme.text_main, 0);
        lv_obj_align(img_left, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text_static(lbl, options[i]);
        lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
        lv_obj_center(lbl);

        lv_obj_t * img_sel = lv_label_create(btn);
        lv_label_set_text(img_sel, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(img_sel, current_theme.text_main, 0);
        lv_obj_align(img_sel, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_add_flag(img_sel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_event_cb(btn, menu_item_event_cb, LV_EVENT_ALL, img_sel);
        // Pass index as user_data
        lv_obj_add_event_cb(btn, wifi_option_select_cb, LV_EVENT_KEY, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, wifi_menu_event_cb, LV_EVENT_KEY, NULL);    

        if(main_group) {
            lv_group_add_obj(main_group, btn);
        }
    }
}

static void wifi_menu_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);

    if(key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        buzzer_play_sound_file("buzzer_click");
        ui_switch_screen(SCREEN_MENU);
    } else if(key == LV_KEY_ENTER) {
        buzzer_play_sound_file("buzzer_hacker_confirm");
    }
}

void ui_wifi_menu_open(void)
{
    if(screen_wifi_menu) {
        lv_obj_del(screen_wifi_menu);
    }

    screen_wifi_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_wifi_menu, current_theme.screen_base, 0);
    lv_obj_remove_flag(screen_wifi_menu, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_wifi_menu);
    footer_ui_create(screen_wifi_menu);
    
    create_wifi_menu(screen_wifi_menu);

    lv_obj_add_event_cb(screen_wifi_menu, wifi_menu_event_cb, LV_EVENT_KEY, NULL);
    lv_screen_load(screen_wifi_menu);
}
