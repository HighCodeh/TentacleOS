#include "battery_settings_ui.h"
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

static lv_obj_t * screen_battery = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static bool power_save = false;
static int timeout_idx = 1;
const char * timeout_options[] = {"30s", "1m", "5m", "NEVER"};
static int perf_idx = 1;
const char * perf_options[] = {"MIN", "BAL", "MAX"};

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

static void update_save_switch(lv_obj_t * cont) {
    lv_obj_t * off_ind = lv_obj_get_child(cont, 0);
    lv_obj_t * on_ind = lv_obj_get_child(cont, 1);
    lv_obj_set_style_bg_opa(off_ind, !power_save ? LV_OPA_COVER : LV_OPA_20, 0);
    lv_obj_set_style_bg_opa(on_ind, power_save ? LV_OPA_COVER : LV_OPA_20, 0);
}

static void battery_item_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * item = lv_event_get_target(e);
    int type = (int)lv_event_get_user_data(e);

    if(code == LV_EVENT_FOCUSED) {
        buzzer_scroll_tick();
        lv_obj_set_style_border_color(item, lv_color_hex(COLOR_BORDER), 0);
        lv_obj_set_style_border_width(item, 2, 0);
        lv_obj_scroll_to_view(item, LV_ANIM_ON);
    } 
    else if(code == LV_EVENT_DEFOCUSED) {
        lv_obj_set_style_border_color(item, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(item, 1, 0);
    }
    else if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        lv_obj_t * input_obj = lv_obj_get_child(item, 2);

        if(type == 0) { // POWER SAVE
            if(key == LV_KEY_ENTER || key == LV_KEY_RIGHT || key == LV_KEY_LEFT) {
                power_save = !power_save;
                update_save_switch(input_obj);
                buzzer_hacker_confirm();
            }
        }
        else if(type == 1) { // TIMEOUT
            if(key == LV_KEY_RIGHT) timeout_idx = (timeout_idx + 1) % 4;
            if(key == LV_KEY_LEFT) timeout_idx = (timeout_idx - 1 + 4) % 4;
            lv_label_set_text_fmt(input_obj, "< %s >", timeout_options[timeout_idx]);
            lv_obj_set_style_text_color(input_obj, lv_color_white(), 0);
            buzzer_scroll_tick();
        }
        else if(type == 2) { // PERFORMANCE
            if(key == LV_KEY_RIGHT) perf_idx = (perf_idx + 1) % 3;
            if(key == LV_KEY_LEFT) perf_idx = (perf_idx - 1 + 3) % 3;
            lv_label_set_text_fmt(input_obj, "< %s >", perf_options[perf_idx]);
            lv_obj_set_style_text_color(input_obj, lv_color_white(), 0);
            buzzer_scroll_tick();
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
    lv_obj_set_size(item, lv_pct(100), 48);
    lv_obj_add_style(item, &style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

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

void ui_battery_settings_open(void) {
    init_styles();
    if(screen_battery) lv_obj_del(screen_battery);

    screen_battery = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_battery, BG_COLOR, 0);
    lv_obj_clear_flag(screen_battery, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_battery);
    footer_ui_create(screen_battery);

    lv_obj_t * menu = lv_obj_create(screen_battery);
    lv_obj_set_size(menu, 230, 180);
    lv_obj_align(menu, LV_ALIGN_CENTER, 0, 5);
    lv_obj_add_style(menu, &style_menu, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * item_save = create_menu_item(menu, LV_SYMBOL_CHARGE, "PWR SAVE");
    lv_obj_t * sw_cont = lv_obj_create(item_save);
    lv_obj_set_size(sw_cont, 56, 30);
    lv_obj_set_style_bg_opa(sw_cont, 0, 0);
    lv_obj_set_style_border_width(sw_cont, 0, 0);
    lv_obj_set_flex_flow(sw_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sw_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(sw_cont, 6, 0);
    lv_obj_clear_flag(sw_cont, LV_OBJ_FLAG_SCROLLABLE);

    for(int i = 0; i < 2; i++) {
        lv_obj_t * b = lv_obj_create(sw_cont);
        lv_obj_set_size(b, 20, 24);
        lv_obj_set_style_bg_color(b, lv_color_white(), 0);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    }
    update_save_switch(sw_cont);
    lv_obj_add_event_cb(item_save, battery_item_event_cb, LV_EVENT_ALL, (void*)0);

    lv_obj_t * item_time = create_menu_item(menu, LV_SYMBOL_EYE_CLOSE, "TIMEOUT");
    lv_obj_t * time_label = lv_label_create(item_time);
    lv_label_set_text_fmt(time_label, "< %s >", timeout_options[timeout_idx]);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_clear_flag(time_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(item_time, battery_item_event_cb, LV_EVENT_ALL, (void*)1);

    lv_obj_t * item_perf = create_menu_item(menu, LV_SYMBOL_SETTINGS, "MODE");
    lv_obj_t * perf_label = lv_label_create(item_perf);
    lv_label_set_text_fmt(perf_label, "< %s >", perf_options[perf_idx]);
    lv_obj_set_style_text_color(perf_label, lv_color_white(), 0);
    lv_obj_clear_flag(perf_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(item_perf, battery_item_event_cb, LV_EVENT_ALL, (void*)2);

    if(main_group) {
        lv_group_add_obj(main_group, item_save);
        lv_group_add_obj(main_group, item_time);
        lv_group_add_obj(main_group, item_perf);
    }

    lv_obj_add_event_cb(screen_battery, screen_back_event_cb, LV_EVENT_KEY, NULL);
    lv_screen_load(screen_battery);
}