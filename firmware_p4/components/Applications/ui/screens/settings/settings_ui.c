#include "settings_ui.h"
#include "interface_settings_ui.h"
#include "display_settings_ui.h"
#include "sound_settings_ui.h"
#include "battery_settings_ui.h"
#include "connection_settings_ui.h"
#include "about_settings_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buzzer.h"
#include "esp_log.h"
#include "esp_timer.h" // Necessário para esp_timer_get_time()

static lv_obj_t * screen_settings = NULL;
static lv_style_t style_menu;
static lv_style_t style_btn;
static bool styles_initialized = false;
static int64_t last_open_time = 0; // Armazena o timestamp de abertura

typedef struct {
    const char * name;
    const char * symbol;
    int target_screen;
} settings_item_t;

static const settings_item_t settings_list[] = {
    {"CONNECTION", LV_SYMBOL_WIFI,         SCREEN_CONNECTION_SETTINGS},
    {"INTERFACE",  LV_SYMBOL_KEYBOARD,     SCREEN_INTERFACE_SETTINGS}, 
    {"DISPLAY",    LV_SYMBOL_IMAGE,        SCREEN_DISPLAY_SETTINGS},
    {"SOUND",      LV_SYMBOL_AUDIO,        SCREEN_SOUND_SETTINGS},
    {"BATTERY",    LV_SYMBOL_BATTERY_FULL, SCREEN_BATTERY_SETTINGS},
    {"ABOUT",      LV_SYMBOL_WARNING,      SCREEN_ABOUT_SETTINGS}
};

#define SETTINGS_COUNT (sizeof(settings_list) / sizeof(settings_list[0]))

static void init_styles(void) {
    if(styles_initialized) {
        lv_style_reset(&style_menu);
        lv_style_reset(&style_btn);
    }
    
    lv_style_init(&style_menu);
    lv_style_set_bg_opa(&style_menu, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_menu, 2);
    lv_style_set_border_color(&style_menu, current_theme.border_accent);
    lv_style_set_radius(&style_menu, 0);
    lv_style_set_pad_all(&style_menu, 8);
    lv_style_set_pad_row(&style_menu, 8);
    
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_btn, current_theme.bg_item_top);
    lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_color(&style_btn, current_theme.border_inactive);
    lv_style_set_radius(&style_btn, 0);
    
    styles_initialized = true;
}

static void settings_item_event_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * label_sel = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    int index = (int)(uintptr_t)lv_obj_get_user_data(btn);

    bool input_locked = (esp_timer_get_time() - last_open_time) < 400000;

    if(code == LV_EVENT_FOCUSED) {
        buzzer_play_sound_file("buzzer_scroll_tick"); 
        lv_obj_remove_flag(label_sel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(btn, current_theme.border_accent, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_scroll_to_view(btn, LV_ANIM_ON);
    }
    else if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(label_sel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(btn, current_theme.border_inactive, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
    }
    else if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        
        if(key == LV_KEY_ESC || key == LV_KEY_LEFT) {
            if(input_locked) return;
            buzzer_play_sound_file("buzzer_scroll_tick");
            ui_switch_screen(SCREEN_MENU);
        }
        else if(key == LV_KEY_ENTER) {
            if(input_locked) return;
            buzzer_play_sound_file("buzzer_hacker_confirm"); 
            ui_switch_screen(settings_list[index].target_screen);
        }
    }
    else if(code == LV_EVENT_CLICKED) {
        if(input_locked) return;
        buzzer_play_sound_file("buzzer_hacker_confirm"); 
        ui_switch_screen(settings_list[index].target_screen);
    }
}

void ui_settings_open(void) {
    last_open_time = esp_timer_get_time();

    init_styles();
    if(screen_settings) lv_obj_del(screen_settings);

    screen_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_settings, current_theme.screen_base, 0);
    
    header_ui_create(screen_settings);
    footer_ui_create(screen_settings);

    lv_obj_t * menu = lv_obj_create(screen_settings);
    lv_obj_set_size(menu, 220, 160);
    lv_obj_align(menu, LV_ALIGN_CENTER, 0, 5);
    lv_obj_add_style(menu, &style_menu, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);

    for(int i = 0; i < SETTINGS_COUNT; i++) {
        lv_obj_t * btn = lv_btn_create(menu);
        lv_obj_set_size(btn, lv_pct(100), 35);
        lv_obj_add_style(btn, &style_btn, 0);
        lv_obj_set_user_data(btn, (void*)(uintptr_t)i);

        lv_obj_t * icon = lv_label_create(btn);
        lv_label_set_text(icon, settings_list[i].symbol);
        lv_obj_set_style_text_color(icon, current_theme.text_main, 0); 
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 5, 0);

        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text_static(lbl, settings_list[i].name);
        lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 30, 0);

        lv_obj_t * label_sel = lv_label_create(btn);
        lv_label_set_text(label_sel, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(label_sel, current_theme.text_main, 0);
        lv_obj_align(label_sel, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_add_flag(label_sel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_event_cb(btn, settings_item_event_cb, LV_EVENT_ALL, label_sel);

        if(main_group) {
            lv_group_add_obj(main_group, btn);
        }
    }

    lv_screen_load(screen_settings);
}
