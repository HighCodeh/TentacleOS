#include "sound_settings_ui.h"
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

static lv_obj_t * screen_sound = NULL;
static lv_style_t style_menu;
static lv_style_t style_item;
static bool styles_initialized = false;

static int master_volume = 3;
static bool buzzer_enabled = true;
static int tone_idx = 0;
const char * tone_options[] = {"CLASSIC", "MODERN", "BLIP", "HACKER"};

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

static void update_volume_bars(lv_obj_t * cont) {
    for(uint32_t i = 0; i < lv_obj_get_child_count(cont); i++) {
        lv_obj_t * bar = lv_obj_get_child(cont, i);
        lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(bar, (i < master_volume) ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

static void update_buzzer_switch(lv_obj_t * cont) {
    lv_obj_t * off_indicator = lv_obj_get_child(cont, 0);
    lv_obj_t * on_indicator = lv_obj_get_child(cont, 1);
    lv_obj_set_style_bg_opa(off_indicator, !buzzer_enabled ? LV_OPA_COVER : LV_OPA_20, 0);
    lv_obj_set_style_bg_opa(on_indicator, buzzer_enabled ? LV_OPA_COVER : LV_OPA_20, 0);
}

static void sound_item_event_cb(lv_event_t * e) {
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

        if(type == 0) {
            if(key == LV_KEY_RIGHT && master_volume < 5) master_volume++;
            if(key == LV_KEY_LEFT && master_volume > 0) master_volume--;
            update_volume_bars(input_obj);
            buzzer_scroll_tick();
        }
        else if(type == 1) {
            if(key == LV_KEY_ENTER || key == LV_KEY_RIGHT || key == LV_KEY_LEFT) {
                buzzer_enabled = !buzzer_enabled;
                update_buzzer_switch(input_obj);
                if(buzzer_enabled) buzzer_hacker_confirm();
            }
        }
        else if(type == 2) {
            if(key == LV_KEY_RIGHT) tone_idx = (tone_idx + 1) % 4;
            if(key == LV_KEY_LEFT) tone_idx = (tone_idx - 1 + 4) % 4;
            lv_label_set_text_fmt(input_obj, "< %s >", tone_options[tone_idx]);
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

void ui_sound_settings_open(void) {
    init_styles();
    if(screen_sound) lv_obj_del(screen_sound);

    screen_sound = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_sound, BG_COLOR, 0);
    lv_obj_clear_flag(screen_sound, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_sound);
    footer_ui_create(screen_sound);

    lv_coord_t menu_h = 240 - 60;

    lv_obj_t * menu = lv_obj_create(screen_sound);
    lv_obj_set_size(menu, 230, menu_h);
    lv_obj_align(menu, LV_ALIGN_CENTER, 0, 5);
    lv_obj_add_style(menu, &style_menu, 0);
    lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(menu, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * item_vol = create_menu_item(menu, LV_SYMBOL_VOLUME_MAX, "VOLUME");
    lv_obj_t * v_cont = lv_obj_create(item_vol);
    lv_obj_set_size(v_cont, 80, 30);
    lv_obj_set_style_bg_opa(v_cont, 0, 0);
    lv_obj_set_style_border_width(v_cont, 0, 0);
    lv_obj_set_flex_flow(v_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(v_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(v_cont, 4, 0);
    lv_obj_clear_flag(v_cont, LV_OBJ_FLAG_SCROLLABLE);

    for(int i = 0; i < 5; i++) {
        lv_obj_t * b = lv_obj_create(v_cont);
        lv_obj_set_size(b, 10, 20);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    }
    update_volume_bars(v_cont);
    lv_obj_add_event_cb(item_vol, sound_item_event_cb, LV_EVENT_ALL, (void*)0);

    lv_obj_t * item_buzzer = create_menu_item(menu, LV_SYMBOL_AUDIO, "BUZZER");
    lv_obj_t * sw_cont = lv_obj_create(item_buzzer);
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
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    }
    update_buzzer_switch(sw_cont);
    lv_obj_add_event_cb(item_buzzer, sound_item_event_cb, LV_EVENT_ALL, (void*)1);

    lv_obj_t * item_tone = create_menu_item(menu, LV_SYMBOL_AUDIO, "TONES");
    lv_obj_t * tone_label = lv_label_create(item_tone);
    lv_label_set_text_fmt(tone_label, "< %s >", tone_options[tone_idx]);
    lv_obj_set_style_text_color(tone_label, lv_color_white(), 0);
    lv_obj_clear_flag(tone_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(item_tone, sound_item_event_cb, LV_EVENT_ALL, (void*)2);

    if(main_group) {
        lv_group_add_obj(main_group, item_vol);
        lv_group_add_obj(main_group, item_buzzer);
        lv_group_add_obj(main_group, item_tone);
    }

    lv_obj_add_event_cb(screen_sound, screen_back_event_cb, LV_EVENT_KEY, NULL);
    lv_screen_load(screen_sound);
}