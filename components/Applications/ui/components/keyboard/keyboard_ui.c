#include "keyboard_ui.h"
#include <string.h>
#include "core/lv_group.h"
#include "ui_manager.h"
#include "buzzer.h"
#include "ui_theme.h"
#include "esp_timer.h"
#include "lv_port_indev.h"

static lv_obj_t * kb_overlay = NULL;
static lv_obj_t * kb_obj = NULL;
static lv_obj_t * kb_ta = NULL;

static lv_style_t style_kb;
static lv_style_t style_kb_btn;
static lv_style_t style_kb_ta;
static bool kb_styles_init = false;
static keyboard_submit_cb_t kb_submit_cb = NULL;
static void * kb_submit_user_data = NULL;

extern lv_group_t * main_group;

static void init_kb_styles(void) {
    if(kb_styles_init) return;

    lv_style_init(&style_kb);
    lv_style_set_bg_color(&style_kb, current_theme.screen_base);
    lv_style_set_border_width(&style_kb, 2);
    lv_style_set_border_color(&style_kb, current_theme.border_accent);
    lv_style_set_radius(&style_kb, 0); 
    lv_style_set_pad_all(&style_kb, 2);

    lv_style_init(&style_kb_btn);
    lv_style_set_radius(&style_kb_btn, 0); 
    lv_style_set_border_width(&style_kb_btn, 1);
    lv_style_set_border_color(&style_kb_btn, current_theme.border_inactive);
    lv_style_set_text_color(&style_kb_btn, lv_color_white());
    lv_style_set_bg_color(&style_kb_btn, current_theme.bg_item_bot);

    lv_style_init(&style_kb_ta);
    lv_style_set_bg_color(&style_kb_ta, lv_color_black());
    lv_style_set_border_color(&style_kb_ta, current_theme.border_accent);
    lv_style_set_border_width(&style_kb_ta, 1);
    lv_style_set_text_color(&style_kb_ta, lv_color_white());
    lv_style_set_radius(&style_kb_ta, 0);

    kb_styles_init = true;
}

static void kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target_kb = lv_event_get_target(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        uint16_t btn_id = lv_keyboard_get_selected_btn(target_kb);
        const char * txt = lv_keyboard_get_btn_text(target_kb, btn_id);
        
        if(txt != NULL && (strcmp(txt, LV_SYMBOL_OK) == 0 || strcmp(txt, "Enter") == 0)) {
            buzzer_play_sound_file("buzzer_hacker_confirm");

            char text_buf[65];
            const char * password = lv_textarea_get_text(kb_ta);
            if(password) {
                strncpy(text_buf, password, sizeof(text_buf) - 1);
                text_buf[sizeof(text_buf) - 1] = '\0';
            } else {
                text_buf[0] = '\0';
            }

            if(kb_submit_cb) {
                kb_submit_cb(text_buf, kb_submit_user_data);
            }

            keyboard_close();
        }
    }
    else if(code == LV_EVENT_CANCEL) {
        keyboard_close();
    }
}

void keyboard_open(lv_obj_t * target_textarea, keyboard_submit_cb_t cb, void * user_data) {
    init_kb_styles();

    (void)target_textarea;

    kb_submit_cb = cb;
    kb_submit_user_data = user_data;
    lv_port_indev_set_keyboard_mode(true);

    if(kb_overlay) keyboard_close();

    kb_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(kb_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(kb_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(kb_overlay, LV_OPA_70, 0);
    lv_obj_set_style_radius(kb_overlay, 0, 0);
    lv_obj_clear_flag(kb_overlay, LV_OBJ_FLAG_SCROLLABLE);

    kb_ta = lv_textarea_create(kb_overlay);
    lv_obj_add_style(kb_ta, &style_kb_ta, 0);
    lv_obj_set_size(kb_ta, 220, 35);
    lv_obj_align(kb_ta, LV_ALIGN_TOP_MID, 0, 10);
    lv_textarea_set_password_mode(kb_ta, true);
    lv_textarea_set_placeholder_text(kb_ta, "ENTER PASSWORD...");
    lv_textarea_set_one_line(kb_ta, true);
    lv_obj_clear_flag(kb_ta, LV_OBJ_FLAG_CLICKABLE);

    kb_obj = lv_keyboard_create(kb_overlay);
    lv_obj_set_size(kb_obj, 230, 115);
    lv_obj_align(kb_obj, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_keyboard_set_mode(kb_obj, LV_KEYBOARD_MODE_TEXT_LOWER); 
    
    lv_obj_add_style(kb_obj, &style_kb, 0);
    lv_obj_add_style(kb_obj, &style_kb_btn, LV_PART_ITEMS);

    lv_obj_set_style_bg_color(kb_obj, current_theme.border_accent, LV_PART_ITEMS | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(kb_obj, lv_color_white(), LV_PART_ITEMS | LV_STATE_FOCUSED);

    lv_keyboard_set_textarea(kb_obj, kb_ta);
    lv_obj_add_event_cb(kb_obj, kb_event_cb, LV_EVENT_ALL, NULL);

    if(main_group) {
        lv_group_remove_all_objs(main_group);
        lv_group_add_obj(main_group, kb_obj);
        lv_group_set_editing(main_group, true);
        lv_group_focus_obj(kb_obj);
    }
}

void keyboard_close(void) {
    if(kb_overlay) {
        if(main_group) {
            lv_group_set_editing(main_group, false);
            lv_group_remove_all_objs(main_group);
        }
        
        lv_obj_del(kb_overlay); 
        kb_overlay = NULL;
        kb_obj = NULL;
        kb_ta = NULL;
        kb_submit_cb = NULL;
        kb_submit_user_data = NULL;
        lv_port_indev_set_keyboard_mode(false);
    }
}
