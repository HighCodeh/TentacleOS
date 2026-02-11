#include "interface_settings_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buzzer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "storage_assets.h"
#include <sys/stat.h>
#include <string.h>

#define INTERFACE_CONFIG_PATH "/assets/config/screen/interface_config.conf"

static const char *TAG = "INTERFACE_UI";
static lv_obj_t * screen_interface = NULL;
static lv_obj_t * menu_container = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

// Referências externas para variáveis gerenciadas pelo ui_theme.c
extern int theme_idx;
extern const char * theme_names[];

const char * theme_options[] = {
    "DEFAULT", "MATRIX", "CYBER", "BLOOD", "TOXIC", "GHOST", 
    "NEON", "AMBER", "TERM", "ICE", "PURPLE", "MIDNIGHT"
};

int header_idx = 0;
const char * header_options[] = {"DEFAULT", "GD TOP", "GD BOT", "MINIMAL"};
bool hide_footer = false;
static int lang_idx = 0;
const char * lang_options[] = {"EN", "PT", "ES", "FR"};

void interface_save_config(void) {
    if (!storage_assets_is_mounted()) return;
    mkdir("/assets/config", 0777);
    mkdir("/assets/config/screen", 0777);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "theme", theme_names[theme_idx]);
    cJSON_AddNumberToObject(root, "header_idx", header_idx);
    cJSON_AddBoolToObject(root, "hide_footer", hide_footer);
    cJSON_AddNumberToObject(root, "lang_idx", lang_idx);
    
    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        FILE *f = fopen(INTERFACE_CONFIG_PATH, "w");
        if (f) { fputs(out, f); fclose(f); }
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

void interface_load_config(void) {
    if (!storage_assets_is_mounted()) return;
    FILE *f = fopen(INTERFACE_CONFIG_PATH, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize + 1);
    if (data) {
        fread(data, 1, fsize, f);
        data[fsize] = 0;
        cJSON *root = cJSON_Parse(data);
        if (root) {
            cJSON *h = cJSON_GetObjectItem(root, "header_idx");
            cJSON *f_sw = cJSON_GetObjectItem(root, "hide_footer");
            cJSON *l = cJSON_GetObjectItem(root, "lang_idx");
            if (cJSON_IsNumber(h)) header_idx = h->valueint;
            if (cJSON_IsBool(f_sw)) hide_footer = cJSON_IsTrue(f_sw);
            if (cJSON_IsNumber(l)) lang_idx = l->valueint;
            cJSON_Delete(root);
        }
        free(data);
    }
    fclose(f);
}

static void init_styles(void) {
    if(styles_initialized) return;
    
    lv_style_init(&style_menu);
    lv_style_set_bg_opa(&style_menu, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_menu, 2);
    lv_style_set_border_color(&style_menu, current_theme.border_accent);
    lv_style_set_radius(&style_menu, 6);
    lv_style_set_pad_all(&style_menu, 8);
    lv_style_set_pad_row(&style_menu, 8);
    
    lv_style_init(&style_item);
    lv_style_set_bg_color(&style_item, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_item, current_theme.bg_item_top);
    lv_style_set_bg_grad_dir(&style_item, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_item, 1);
    lv_style_set_border_color(&style_item, current_theme.border_inactive);
    lv_style_set_radius(&style_item, 4);
    
    styles_initialized = true;
}

static void refresh_styles(void) {
    lv_style_set_border_color(&style_menu, current_theme.border_accent);
    lv_style_set_bg_color(&style_item, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_item, current_theme.bg_item_top);
    lv_style_set_border_color(&style_item, current_theme.border_inactive);
}

static void refresh_focused_item(void) {
    if (!main_group) return;
    lv_obj_t * focused = lv_group_get_focused(main_group);
    if (focused) {
        lv_obj_set_style_border_color(focused, current_theme.border_accent, 0);
    }
}

static void refresh_menu_items(void) {
    if (!menu_container) return;
    uint32_t child_count = lv_obj_get_child_count(menu_container);
    for(uint32_t i = 0; i < child_count; i++) {
        lv_obj_t * item = lv_obj_get_child(menu_container, i);
        lv_obj_t * icon = lv_obj_get_child(item, 0);
        lv_obj_t * label = lv_obj_get_child(item, 1);
        lv_obj_t * value = lv_obj_get_child(item, 2);
        if (icon) lv_obj_set_style_text_color(icon, current_theme.text_main, 0);
        if (label) lv_obj_set_style_text_color(label, current_theme.text_main, 0);
        if (value) lv_obj_set_style_text_color(value, current_theme.text_main, 0);
    }
}

static void update_footer_switch(lv_obj_t * cont) {
    lv_obj_t * b_off = lv_obj_get_child(cont, 0);
    lv_obj_t * b_on = lv_obj_get_child(cont, 1);
    if (hide_footer) {
        lv_obj_set_style_bg_opa(b_off, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(b_on, LV_OPA_20, 0);
    } else {
        lv_obj_set_style_bg_opa(b_off, LV_OPA_20, 0);
        lv_obj_set_style_bg_opa(b_on, LV_OPA_COVER, 0);
    }
}

static void refresh_ui_layout(void) {
    if (!screen_interface) return;
    for(int32_t i = lv_obj_get_child_count(screen_interface) - 1; i >= 0; i--) {
        lv_obj_t * child = lv_obj_get_child(screen_interface, i);
        lv_coord_t h = lv_obj_get_height(child);
        lv_coord_t y = lv_obj_get_y(child);
        if((h <= 40 && y <= 5) || (h <= 40 && y >= 200)) {
            lv_obj_del(child);
        }
    }
    lv_obj_set_style_bg_color(screen_interface, current_theme.screen_base, 0);
    header_ui_create(screen_interface);
    footer_ui_create(screen_interface);
    refresh_focused_item();
    refresh_menu_items();
    lv_obj_invalidate(screen_interface);
}

static void interface_item_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * item = lv_event_get_target(e);
    int type = (int)(uintptr_t)lv_event_get_user_data(e);

    if(code == LV_EVENT_FOCUSED) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        lv_obj_set_style_border_color(item, current_theme.border_accent, 0);
        lv_obj_set_style_border_width(item, 2, 0);
    } 
    else if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_border_color(item, current_theme.border_inactive, 0);
        lv_obj_set_style_border_width(item, 1, 0);
    }
    else if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        lv_obj_t * input_obj = lv_obj_get_child(item, 2);
        bool changed = false;

        if(key == LV_KEY_ESC) {
            buzzer_play_sound_file("buzzer_click");
            ui_switch_screen(SCREEN_SETTINGS);
            return;
        }

        if(type == 0) {
            // Theme selector
            if(key == LV_KEY_RIGHT) { 
                theme_idx = (theme_idx + 1) % 12; 
                changed = true; 
            }
            if(key == LV_KEY_LEFT) { 
                theme_idx = (theme_idx - 1 + 12) % 12; 
                changed = true; 
            }
            if(changed) {
                lv_label_set_text_fmt(input_obj, "< %s >", theme_options[theme_idx]);
                lv_obj_set_style_text_color(input_obj, current_theme.text_main, 0);
                ui_theme_load_idx(theme_idx);
                refresh_styles();
                interface_save_config();
                refresh_ui_layout();
                buzzer_play_sound_file("buzzer_scroll_tick");
            }
        }
        else if(type == 1) {
            // Header selector
            if(key == LV_KEY_RIGHT) { 
                header_idx = (header_idx + 1) % 4; 
                changed = true; 
            }
            if(key == LV_KEY_LEFT) { 
                header_idx = (header_idx - 1 + 4) % 4; 
                changed = true; 
            }
            if(changed) {
                lv_label_set_text_fmt(input_obj, "< %s >", header_options[header_idx]);
                interface_save_config();
                refresh_ui_layout();
                buzzer_play_sound_file("buzzer_scroll_tick");
            }
        }
        else if(type == 2) {
            // Footer toggle
            if(key == LV_KEY_ENTER || key == LV_KEY_RIGHT || key == LV_KEY_LEFT) {
                hide_footer = !hide_footer;
                update_footer_switch(input_obj);
                interface_save_config();
                refresh_ui_layout();
                buzzer_play_sound_file("buzzer_hacker_confirm");
            }
        }
    }
}

static lv_obj_t * create_menu_item(lv_obj_t * parent, const char * symbol, const char * name) {
    lv_obj_t * item = lv_obj_create(parent);
    lv_obj_set_size(item, lv_pct(100), 48);
    lv_obj_add_style(item, &style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t * icon = lv_label_create(item);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, current_theme.text_main, 0);
    
    lv_obj_t * label = lv_label_create(item);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, current_theme.text_main, 0);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_margin_left(label, 10, 0);
    
    return item;
}

void ui_interface_settings_open(void) {
    interface_load_config();
    init_styles();
    
    if(screen_interface) lv_obj_del(screen_interface);
    screen_interface = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_interface, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_interface, LV_OBJ_FLAG_SCROLLABLE);
    
    header_ui_create(screen_interface);
    footer_ui_create(screen_interface);
    
    menu_container = lv_obj_create(screen_interface);
    lv_obj_set_size(menu_container, 230, 180);
    lv_obj_align(menu_container, LV_ALIGN_CENTER, 0, 5);
    lv_obj_add_style(menu_container, &style_menu, 0);
    lv_obj_set_flex_flow(menu_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(menu_container, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * item_theme = create_menu_item(menu_container, LV_SYMBOL_TINT, "THEME");
    lv_obj_t * theme_label = lv_label_create(item_theme);
    lv_label_set_text_fmt(theme_label, "< %s >", theme_options[theme_idx]);
    lv_obj_set_style_text_color(theme_label, lv_color_white(), 0);
    lv_obj_add_event_cb(item_theme, interface_item_event_cb, LV_EVENT_ALL, (void*)0);

    lv_obj_t * item_head = create_menu_item(menu_container, LV_SYMBOL_UP, "HEADER");
    lv_obj_t * head_label = lv_label_create(item_head);
    lv_label_set_text_fmt(head_label, "< %s >", header_options[header_idx]);
    lv_obj_set_style_text_color(head_label, current_theme.text_main, 0);
    lv_obj_set_style_text_color(head_label, lv_color_white(), 0);
    lv_obj_add_event_cb(item_head, interface_item_event_cb, LV_EVENT_ALL, (void*)1);
    
    lv_obj_t * item_foot = create_menu_item(menu_container, LV_SYMBOL_DOWN, "FOOTER");
    
    lv_obj_t * sw_cont = lv_obj_create(item_foot);
    lv_obj_set_size(sw_cont, 60, 32);
    lv_obj_set_style_bg_opa(sw_cont, 0, 0);
    lv_obj_set_style_border_width(sw_cont, 0, 0);
    lv_obj_set_flex_flow(sw_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sw_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(sw_cont, 0, 0);
    lv_obj_set_style_pad_gap(sw_cont, 4, 0);
    
    lv_obj_clear_flag(sw_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(sw_cont, LV_SCROLLBAR_MODE_OFF);
    
    for(int i = 0; i < 2; i++) {
        lv_obj_t * b = lv_obj_create(sw_cont);
        lv_obj_set_size(b, 24, 26);
        lv_obj_set_style_bg_color(b, current_theme.text_main, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    update_footer_switch(sw_cont);
    lv_obj_add_event_cb(item_foot, interface_item_event_cb, LV_EVENT_ALL, (void*)2);
    
    if(main_group) {
        lv_group_add_obj(main_group, item_theme);
        lv_group_add_obj(main_group, item_head);
        lv_group_add_obj(main_group, item_foot);
        lv_group_focus_obj(item_theme);
    }
    
    lv_screen_load(screen_interface);
}