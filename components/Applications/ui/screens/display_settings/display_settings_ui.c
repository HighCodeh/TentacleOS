#include "display_settings_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buzzer.h"
#include "esp_log.h"

#define BG_COLOR            lv_color_black()
#define COLOR_BORDER        0x834EC6
#define COLOR_GRADIENT_TOP  0x000000
#define COLOR_GRADIENT_BOT  0x2E0157

static lv_obj_t * screen_display = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static int brightness_val = 3;
static bool footer_enabled = true;
static int wallpaper_idx = 0;
const char * wallpaper_options[] = {"NEBULA", "CYBER", "DARK", "RETRO"};

static void init_styles(void) {
    if(styles_initialized) return;

    lv_style_init(&style_menu);
    lv_style_set_bg_opa(&style_menu, LV_OPA_TRANSP);
    lv_style_set_border_width(&style_menu, 2);
    lv_style_set_border_color(&style_menu, lv_color_hex(COLOR_BORDER));
    lv_style_set_radius(&style_menu, 6);
    lv_style_set_pad_all(&style_menu, 8);
    lv_style_set_pad_row(&style_menu, 8);

    lv_style_init(&style_item);
    lv_style_set_bg_color(&style_item, lv_color_hex(COLOR_GRADIENT_BOT));
    lv_style_set_bg_grad_color(&style_item, lv_color_hex(COLOR_GRADIENT_TOP));
    lv_style_set_bg_grad_dir(&style_item, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_item, 1);
    lv_style_set_border_color(&style_item, lv_color_hex(0x333333));
    lv_style_set_radius(&style_item, 4);

    styles_initialized = true;
}

static void update_brightness_bars(lv_obj_t * cont) {
    for(uint32_t i = 0; i < lv_obj_get_child_count(cont); i++) {
        lv_obj_t * bar = lv_obj_get_child(cont, i);
        lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(bar, (i < brightness_val) ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

static void anim_sw_cb(void * var, int32_t v) {
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void display_item_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * item = lv_event_get_target(e);
    int type = (int)lv_event_get_user_data(e);

    if(code == LV_EVENT_FOCUSED) {
        buzzer_scroll_tick();
        lv_obj_set_style_border_color(item, lv_color_hex(COLOR_BORDER), 0);
        lv_obj_set_style_border_width(item, 2, 0);
    } 
    else if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_border_color(item, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(item, 1, 0);
    }
    else if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        
        if(type == 0) { 
            lv_obj_t * bars_cont = lv_obj_get_child(item, 2);
            if(key == LV_KEY_RIGHT && brightness_val < 6) brightness_val++;
            if(key == LV_KEY_LEFT && brightness_val > 0) brightness_val--;
            update_brightness_bars(bars_cont);
        }
        else if(type == 1) {
            if(key == LV_KEY_ENTER || key == LV_KEY_RIGHT || key == LV_KEY_LEFT) {
                footer_enabled = !footer_enabled;
                lv_obj_t * sw_cont = lv_obj_get_child(item, 2);
                lv_obj_t * knob = lv_obj_get_child(sw_cont, 0);
                
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, knob);
                lv_anim_set_values(&a, lv_obj_get_x(knob), footer_enabled ? 16 : 2);
                lv_anim_set_time(&a, 150);
                lv_anim_set_exec_cb(&a, anim_sw_cb);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_start(&a);

                lv_obj_set_style_bg_color(sw_cont, footer_enabled ? lv_color_hex(COLOR_BORDER) : lv_color_hex(0x444444), LV_ANIM_ON);
                
                buzzer_hacker_confirm();
            }
        }
        else if(type == 2) {
            lv_obj_t * opt_label = lv_obj_get_child(item, 2);
            if(key == LV_KEY_RIGHT) wallpaper_idx = (wallpaper_idx + 1) % 4;
            if(key == LV_KEY_LEFT) wallpaper_idx = (wallpaper_idx - 1 + 4) % 4;
            
            char buf[32];
            snprintf(buf, sizeof(buf), "< %s >", wallpaper_options[wallpaper_idx]);
            lv_label_set_text(opt_label, buf);
        }
    }
}

static void screen_back_event_cb(lv_event_t * e) {
    uint32_t key = lv_event_get_key(e);
    if(key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        ui_switch_screen(SCREEN_SETTINGS);
    }
}

static lv_obj_t * create_menu_item(lv_obj_t * parent, const char * symbol, const char * name) {
    lv_obj_t * item = lv_obj_create(parent);
    lv_obj_set_size(item, lv_pct(100), 40);
    lv_obj_add_style(item, &style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_right(item, 10, 0);

    lv_obj_t * icon = lv_label_create(item);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);

    lv_obj_t * label = lv_label_create(item);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_margin_left(label, 8, 0);

    return item;
}

void ui_display_settings_open(void) {
    init_styles();
    if(screen_display) lv_obj_del(screen_display);

    screen_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_display, BG_COLOR, 0);

    header_ui_create(screen_display);
    footer_ui_create(screen_display);

    lv_obj_t * menu = lv_obj_create(screen_display);
    lv_obj_set_size(menu, 220, 160);
    lv_obj_align(menu, LV_ALIGN_CENTER, 0, 5);
    lv_obj_add_style(menu, &style_menu, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * item_sw = create_menu_item(menu, LV_SYMBOL_IMAGE, "ENABLE FOOTER");
    
    lv_obj_t * sw_cont = lv_obj_create(item_sw);
    lv_obj_set_size(sw_cont, 34, 18);
    lv_obj_set_style_bg_color(sw_cont, footer_enabled ? lv_color_hex(COLOR_BORDER) : lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(sw_cont, 0, 0);
    lv_obj_set_style_radius(sw_cont, 2, 0);
    lv_obj_clear_flag(sw_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * knob = lv_obj_create(sw_cont);
    lv_obj_set_size(knob, 16, 14); 
    lv_obj_set_style_bg_color(knob, lv_color_white(), 0);
    lv_obj_set_style_radius(knob, 1, 0);
    lv_obj_set_style_border_width(knob, 0, 0);
    lv_obj_align(knob, LV_ALIGN_LEFT_MID, footer_enabled ? 16 : 2, 0);
    
    lv_obj_add_event_cb(item_sw, display_item_event_cb, LV_EVENT_ALL, (void*)1);

    lv_obj_t * item_br = create_menu_item(menu, LV_SYMBOL_SETTINGS, "BRIGHTNESS");
    lv_obj_t * b_cont = lv_obj_create(item_br);
    lv_obj_set_size(b_cont, 75, 20);
    lv_obj_set_style_bg_opa(b_cont, 0, 0);
    lv_obj_set_style_border_width(b_cont, 0, 0);
    lv_obj_set_flex_flow(b_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(b_cont, 3, 0);
    lv_obj_clear_flag(b_cont, LV_OBJ_FLAG_SCROLLABLE);

    for(int i = 0; i < 6; i++) {
        lv_obj_t * b = lv_obj_create(b_cont);
        lv_obj_set_size(b, 6, 16);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
    }
    update_brightness_bars(b_cont);
    lv_obj_add_event_cb(item_br, display_item_event_cb, LV_EVENT_ALL, (void*)0);

    lv_obj_t * item_wall = create_menu_item(menu, LV_SYMBOL_EDIT, "THEME");
    lv_obj_t * wall_label = lv_label_create(item_wall);
    char buf[32];
    snprintf(buf, sizeof(buf), "< %s >", wallpaper_options[wallpaper_idx]);
    lv_label_set_text(wall_label, buf);
    lv_obj_set_style_text_color(wall_label, lv_color_white(), 0);
    lv_obj_add_event_cb(item_wall, display_item_event_cb, LV_EVENT_ALL, (void*)2);

    if(main_group) {
        lv_group_add_obj(main_group, item_sw);
        lv_group_add_obj(main_group, item_br);
        lv_group_add_obj(main_group, item_wall);
    }

    lv_obj_add_event_cb(screen_display, screen_back_event_cb, LV_EVENT_KEY, NULL);
    lv_screen_load(screen_display);
}