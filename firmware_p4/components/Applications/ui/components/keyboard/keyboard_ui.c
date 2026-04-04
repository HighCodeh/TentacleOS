#include "keyboard_ui.h"
#include "ui_theme.h"
#include <string.h>
#include "core/lv_group.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "st7789.h"

#define BORDER_COLOR    current_theme.border_interface
#define ITEM_BORDER     current_theme.border_accent
#define KB_BG_TOP       current_theme.bg_secondary
#define KB_BG_BOT       current_theme.bg_item_bot
#define KB_BTN_BG       current_theme.bg_item_bot
#define KB_BTN_GRAD     current_theme.border_inactive
#define KB_BTN_BORDER   current_theme.border_interface
#define KB_BTN_FOCUS    current_theme.border_accent
#define KB_TA_BG        current_theme.screen_base

#define OUTER_BORDER    4
#define TOP_BORDER_H    46
#define KB_H            160

static lv_obj_t * kb_screen = NULL;
static lv_obj_t * kb_obj = NULL;
static lv_obj_t * kb_ta = NULL;

static keyboard_submit_cb_t kb_submit_cb = NULL;
static void * kb_submit_user_data = NULL;

extern lv_group_t * main_group;

static void kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * target_kb = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        uint16_t btn_id = lv_keyboard_get_selected_btn(target_kb);
        const char * txt = lv_keyboard_get_btn_text(target_kb, btn_id);

        if (txt && (strcmp(txt, LV_SYMBOL_OK) == 0 || strcmp(txt, "Enter") == 0)) {
            char text_buf[65];
            const char * input = lv_textarea_get_text(kb_ta);
            if (input) {
                strncpy(text_buf, input, sizeof(text_buf) - 1);
                text_buf[sizeof(text_buf) - 1] = '\0';
            } else {
                text_buf[0] = '\0';
            }
            if (kb_submit_cb) kb_submit_cb(text_buf, kb_submit_user_data);
            keyboard_close();
        }
    } else if (code == LV_EVENT_CANCEL) {
        keyboard_close();
    }
}

void keyboard_open(lv_obj_t * target_textarea, keyboard_submit_cb_t cb, void * user_data) {
    (void)target_textarea;

    kb_submit_cb = cb;
    kb_submit_user_data = user_data;
    lv_port_indev_set_keyboard_mode(true);

    if (kb_screen) keyboard_close();

    kb_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(kb_screen, LCD_H_RES, LCD_V_RES);
    lv_obj_align(kb_screen, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(kb_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(kb_screen, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(kb_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(kb_screen, 0, 0);
    lv_obj_set_style_border_width(kb_screen, OUTER_BORDER, 0);
    lv_obj_set_style_border_color(kb_screen, BORDER_COLOR, 0);
    lv_obj_set_style_radius(kb_screen, 0, 0);
    lv_obj_move_foreground(kb_screen);

    lv_obj_t * top_area = lv_obj_create(kb_screen);
    lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
    lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_area, 3, 0);
    lv_obj_set_style_border_color(top_area, BORDER_COLOR, 0);
    lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(top_area, 0, 0);
    lv_obj_set_style_pad_all(top_area, 0, 0);

    lv_obj_t * title_bar = lv_obj_create(top_area);
    lv_obj_set_size(title_bar, 170, 30);
    lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_bar, 12, 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(title_bar, current_theme.border_interface, 0);
    lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(title_bar, 2, 0);
    lv_obj_set_style_border_color(title_bar, ITEM_BORDER, 0);

    lv_obj_t * title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, "KEYBOARD");
    lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(title_lbl);

    int ta_y = TOP_BORDER_H + 10;
    int ta_h = LCD_V_RES - TOP_BORDER_H - KB_H - OUTER_BORDER - 20;

    kb_ta = lv_textarea_create(kb_screen);
    lv_obj_set_size(kb_ta, LCD_H_RES - OUTER_BORDER * 2 - 20, ta_h > 60 ? 40 : 30);
    lv_obj_align(kb_ta, LV_ALIGN_TOP_MID, 0, ta_y + (ta_h - 40) / 2);
    lv_textarea_set_password_mode(kb_ta, false);
    lv_textarea_set_placeholder_text(kb_ta, "TYPE HERE...");
    lv_textarea_set_one_line(kb_ta, true);
    lv_obj_remove_flag(kb_ta, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_bg_color(kb_ta, KB_TA_BG, 0);
    lv_obj_set_style_bg_opa(kb_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb_ta, 2, 0);
    lv_obj_set_style_border_color(kb_ta, ITEM_BORDER, 0);
    lv_obj_set_style_radius(kb_ta, 10, 0);
    lv_obj_set_style_text_color(kb_ta, current_theme.text_main, 0);
    lv_obj_set_style_text_font(kb_ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_left(kb_ta, 10, 0);

    kb_obj = lv_keyboard_create(kb_screen);
    lv_obj_set_size(kb_obj, LCD_H_RES - OUTER_BORDER * 2 - 4, KB_H);
    lv_obj_align(kb_obj, LV_ALIGN_BOTTOM_MID, 0, -OUTER_BORDER - 2);
    lv_keyboard_set_mode(kb_obj, LV_KEYBOARD_MODE_TEXT_LOWER);

    lv_obj_set_style_bg_color(kb_obj, KB_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(kb_obj, KB_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(kb_obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(kb_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb_obj, 2, 0);
    lv_obj_set_style_border_color(kb_obj, BORDER_COLOR, 0);
    lv_obj_set_style_radius(kb_obj, 12, 0);
    lv_obj_set_style_pad_all(kb_obj, 6, 0);
    lv_obj_set_style_pad_gap(kb_obj, 4, 0);

    lv_obj_set_style_bg_color(kb_obj, KB_BTN_BG, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_color(kb_obj, KB_BTN_GRAD, LV_PART_ITEMS);
    lv_obj_set_style_bg_grad_dir(kb_obj, LV_GRAD_DIR_VER, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb_obj, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb_obj, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb_obj, KB_BTN_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb_obj, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb_obj, current_theme.text_main, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb_obj, &lv_font_montserrat_12, LV_PART_ITEMS);

    lv_obj_set_style_bg_color(kb_obj, KB_BTN_FOCUS, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_grad_color(kb_obj, current_theme.border_accent, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_grad_dir(kb_obj, LV_GRAD_DIR_VER, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_color(kb_obj, current_theme.border_accent, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(kb_obj, 2, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(kb_obj, current_theme.text_main, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);

    lv_obj_set_style_bg_color(kb_obj, current_theme.border_accent, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb_obj, current_theme.screen_base, LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_keyboard_set_textarea(kb_obj, kb_ta);
    lv_obj_add_event_cb(kb_obj, kb_event_cb, LV_EVENT_ALL, NULL);

    if (main_group) {
        lv_group_remove_all_objs(main_group);
        lv_group_add_obj(main_group, kb_obj);
        lv_group_set_editing(main_group, true);
        lv_group_focus_obj(kb_obj);
    }
}

void keyboard_close(void) {
    if (kb_screen) {
        if (main_group) {
            lv_group_set_editing(main_group, false);
            lv_group_remove_all_objs(main_group);
        }
        lv_obj_del(kb_screen);
        kb_screen = NULL;
        kb_obj = NULL;
        kb_ta = NULL;
        kb_submit_cb = NULL;
        kb_submit_user_data = NULL;
        lv_port_indev_set_keyboard_mode(false);
    }
}
