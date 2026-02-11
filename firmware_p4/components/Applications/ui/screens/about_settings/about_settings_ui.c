#include "about_settings_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buzzer.h"
#include "esp_log.h"

static lv_obj_t * screen_about = NULL;
static lv_style_t style_info_box;

static void init_styles(void) {
    static bool styles_initialized = false;
    if(styles_initialized) {
        lv_style_reset(&style_info_box);
    }

    lv_style_init(&style_info_box);
    lv_style_set_bg_color(&style_info_box, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_info_box, current_theme.bg_item_top);
    lv_style_set_bg_grad_dir(&style_info_box, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_info_box, 2);
    lv_style_set_border_color(&style_info_box, current_theme.border_accent);
    lv_style_set_radius(&style_info_box, 8);
    lv_style_set_pad_all(&style_info_box, 12);

    styles_initialized = true;
}

static void screen_back_event_cb(lv_event_t * e) {
    uint32_t key = lv_event_get_key(e);
    if(key == LV_KEY_ESC || key == LV_KEY_LEFT || key == LV_KEY_ENTER) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        ui_switch_screen(SCREEN_SETTINGS);
    }
}

void ui_about_settings_open(void) {
    init_styles();
    if(screen_about) lv_obj_del(screen_about);

    screen_about = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_about, current_theme.screen_base, 0);
    lv_obj_clear_flag(screen_about, LV_OBJ_FLAG_SCROLLABLE);

    header_ui_create(screen_about);
    footer_ui_create(screen_about);

    lv_obj_t * info_box = lv_obj_create(screen_about);
    lv_obj_set_size(info_box, 220, 150);
    lv_obj_align(info_box, LV_ALIGN_CENTER, 0, 5);
    lv_obj_add_style(info_box, &style_info_box, 0);
    lv_obj_set_flex_flow(info_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title = lv_label_create(info_box);
    lv_label_set_text(title, "TENTACLE OS");
    lv_obj_set_style_text_color(title, current_theme.text_main, 0);
    lv_obj_set_style_margin_bottom(title, 10, 0);

    lv_obj_t * version = lv_label_create(info_box);
    lv_label_set_text(version, "Version: DEV");
    lv_obj_set_style_text_color(version, current_theme.text_main, 0);

    lv_obj_t * hardware = lv_label_create(info_box);
    lv_label_set_text(hardware, "HW: ESP32-S3");
    lv_obj_set_style_text_color(hardware, current_theme.text_main, 0);

    lv_obj_t * build = lv_label_create(info_box);
    lv_label_set_text(build, "Build: Jan 2026");
    lv_obj_set_style_text_color(build, current_theme.text_main, 0);

    lv_obj_t * hint = lv_label_create(info_box);
    lv_label_set_text(hint, "< PRESS TO EXIT >");
    lv_obj_set_style_text_color(hint, current_theme.text_main, 0);
    lv_obj_set_style_margin_top(hint, 15, 0);

    lv_obj_add_event_cb(screen_about, screen_back_event_cb, LV_EVENT_KEY, NULL);
    
    if(main_group) {
        lv_group_add_obj(main_group, screen_about);
        lv_group_focus_obj(screen_about);
    }

    lv_screen_load(screen_about);
}