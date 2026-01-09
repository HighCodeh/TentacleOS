#include "display_settings_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "esp_log.h"

#define BG_COLOR            lv_color_black()
#define COLOR_BORDER        0x834EC6
#define COLOR_GRADIENT_TOP  0x1A0033
#define COLOR_GRADIENT_BOT  0x000000

static lv_obj_t * screen_display = NULL;
static lv_style_t style_item_cont;
static lv_style_t style_item_focused;

static int brightness_val = 3;
static bool footer_enabled = true;
static int wallpaper_idx = 0;
const char * wallpaper_options[] = {"Nebula", "Cyber", "Dark", "Retro"};

static void init_styles(void) {
    static bool styles_initialized = false;
    if(styles_initialized) return;

    lv_style_init(&style_item_cont);
    lv_style_set_bg_color(&style_item_cont, lv_color_hex(COLOR_GRADIENT_TOP));
    lv_style_set_bg_grad_color(&style_item_cont, lv_color_hex(COLOR_GRADIENT_BOT));
    lv_style_set_bg_grad_dir(&style_item_cont, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_item_cont, 2);
    lv_style_set_border_color(&style_item_cont, lv_color_hex(0x3D107A));
    lv_style_set_radius(&style_item_cont, 4);
    lv_style_set_pad_all(&style_item_cont, 8);

    lv_style_init(&style_item_focused);
    lv_style_set_border_color(&style_item_focused, lv_color_hex(COLOR_BORDER));
    lv_style_set_border_width(&style_item_focused, 3);

    styles_initialized = true;
}

static void update_brightness_bars(lv_obj_t * cont) {
    for(uint32_t i = 0; i < lv_obj_get_child_count(cont); i++) {
        lv_obj_t * bar = lv_obj_get_child(cont, i);
        lv_obj_set_style_bg_opa(bar, (i < brightness_val) ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

static void display_item_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * item = lv_event_get_target(e);
    int type = (int)lv_event_get_user_data(e);

    if(code == LV_EVENT_KEY) {
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
                lv_obj_t * sw = lv_obj_get_child(item, 2);
                if(footer_enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
                else lv_obj_clear_state(sw, LV_STATE_CHECKED);
            }
        }

        else if(type == 2) {
            lv_obj_t * opt_label = lv_obj_get_child(item, 2);
            if(key == LV_KEY_RIGHT) wallpaper_idx = (wallpaper_idx + 1) % 4;
            if(key == LV_KEY_LEFT) wallpaper_idx = (wallpaper_idx - 1 + 4) % 4;
            lv_label_set_text(opt_label, wallpaper_options[wallpaper_idx]);
        }
    }
}

static void screen_back_event_cb(lv_event_t * e) {
    uint32_t key = lv_event_get_key(e);
    if(key == LV_KEY_ESC || key == LV_KEY_LEFT) {
        ui_switch_screen(SCREEN_SETTINGS);
    }
}

static lv_obj_t * create_item_container(lv_obj_t * parent, const char * symbol, const char * name) {
    lv_obj_t * item = lv_obj_create(parent);
    lv_obj_set_size(item, lv_pct(100), 45);
    lv_obj_add_style(item, &style_item_cont, 0);
    lv_obj_add_style(item, &style_item_focused, LV_STATE_FOCUSED);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * icon = lv_label_create(item);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);

    lv_obj_t * label = lv_label_create(item);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_style_margin_left(label, 10, 0);

    return item;
}

void ui_display_settings_open(void) {
    init_styles();
    if(screen_display) lv_obj_del(screen_display);

    screen_display = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_display, BG_COLOR, 0);

    header_ui_create(screen_display);
    footer_ui_create(screen_display);

    lv_obj_t * list_cont = lv_obj_create(screen_display);
    lv_obj_set_size(list_cont, 230, 165);
    lv_obj_align(list_cont, LV_ALIGN_CENTER, 0, 5);
    lv_obj_set_style_bg_opa(list_cont, 0, 0);
    lv_obj_set_style_border_width(list_cont, 0, 0);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(list_cont, 8, 0); 
    lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * item_sw = create_item_container(list_cont, LV_SYMBOL_IMAGE, "FOOTER");
    lv_obj_t * sw = lv_checkbox_create(item_sw);
    lv_checkbox_set_text(sw, "");
    lv_obj_set_style_radius(sw, 0, LV_PART_INDICATOR);
    if(footer_enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(item_sw, display_item_event_cb, LV_EVENT_KEY, (void*)1);

    lv_obj_t * item_br = create_item_container(list_cont, LV_SYMBOL_SETTINGS, "BRIGHTNESS");
    lv_obj_t * b_cont = lv_obj_create(item_br);
    lv_obj_set_size(b_cont, 75, 18);
    lv_obj_set_style_bg_opa(b_cont, 0, 0);
    lv_obj_set_style_border_width(b_cont, 0, 0);
    lv_obj_set_flex_flow(b_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(b_cont, 3, 0);
    for(int i = 0; i < 6; i++) {
        lv_obj_t * b = lv_obj_create(b_cont);
        lv_obj_set_size(b, 7, 12);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
    }
    update_brightness_bars(b_cont);
    lv_obj_add_event_cb(item_br, display_item_event_cb, LV_EVENT_KEY, (void*)0);

    lv_obj_t * item_wall = create_item_container(list_cont, LV_SYMBOL_EDIT, "THEME");
    lv_obj_t * wall_label = lv_label_create(item_wall);
    lv_label_set_text(wall_label, wallpaper_options[wallpaper_idx]);
    lv_obj_set_style_text_color(wall_label, lv_color_white(), 0);
    lv_obj_add_event_cb(item_wall, display_item_event_cb, LV_EVENT_KEY, (void*)2);

    if(main_group) {
        lv_group_add_obj(main_group, item_sw);
        lv_group_add_obj(main_group, item_br);
        lv_group_add_obj(main_group, item_wall);
    }

    lv_obj_add_event_cb(screen_display, screen_back_event_cb, LV_EVENT_KEY, NULL);
    lv_screen_load(screen_display);
}