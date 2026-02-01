#include "msgbox_ui.h"
#include "core/lv_group.h"
#include "buzzer.h"
#include "esp_timer.h"
#include "ui_theme.h"

static lv_obj_t * msgbox_overlay = NULL;
static msgbox_cb_t current_cb = NULL;
static lv_style_t style_box;
static lv_style_t style_btn;
static bool styles_init = false;
static int64_t msgbox_open_time = 0;

extern lv_group_t * main_group;

static void init_msgbox_styles(void) {
    if(styles_init) return;

    lv_style_init(&style_box);
    lv_style_set_bg_color(&style_box, current_theme.screen_base);
    lv_style_set_border_width(&style_box, 2);
    lv_style_set_border_color(&style_box, current_theme.border_accent);
    lv_style_set_radius(&style_box, 10);
    lv_style_set_pad_all(&style_box, 10);

    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, current_theme.bg_item_bot);
    lv_style_set_bg_grad_color(&style_btn, current_theme.bg_item_top);
    lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&style_btn, 1);
    lv_style_set_border_color(&style_btn, current_theme.border_inactive);
    lv_style_set_radius(&style_btn, 4);
    lv_style_set_text_color(&style_btn, lv_color_white());

    styles_init = true;
}

static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    bool confirm = (bool)(uintptr_t)lv_event_get_user_data(e);

    if(code == LV_EVENT_CLICKED) {
        if ((esp_timer_get_time() - msgbox_open_time) < 200000) {
            return;
        }
        buzzer_play_sound_file(confirm ? "buzzer_hacker_confirm" : "buzzer_scroll_tick");
        if(current_cb) current_cb(confirm);
        msgbox_close();
    } else if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if(key == LV_KEY_ENTER) {
            if ((esp_timer_get_time() - msgbox_open_time) < 200000) {
                return;
            }
            buzzer_play_sound_file(confirm ? "buzzer_hacker_confirm" : "buzzer_scroll_tick");
            if(current_cb) current_cb(confirm);
            msgbox_close();
        }
    }
}

static void msgbox_overlay_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if(key == LV_KEY_ESC || key == LV_KEY_BACKSPACE) {
            if ((esp_timer_get_time() - msgbox_open_time) < 200000) {
                return;
            }
            buzzer_play_sound_file("buzzer_scroll_tick");
            if(current_cb) current_cb(false);
            msgbox_close();
        }
    }
}

void msgbox_open(const char * icon, const char * msg, const char * btn_ok, const char * btn_cancel, msgbox_cb_t cb) {
    init_msgbox_styles();
    if(msgbox_overlay) msgbox_close();

    current_cb = cb;
    msgbox_open_time = esp_timer_get_time();

    msgbox_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(msgbox_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(msgbox_overlay, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(msgbox_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(msgbox_overlay, 0, 0);
    lv_obj_set_style_outline_width(msgbox_overlay, 0, 0);
    lv_obj_clear_flag(msgbox_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(msgbox_overlay, msgbox_overlay_event_cb, LV_EVENT_KEY, NULL);

    lv_obj_t * box = lv_obj_create(msgbox_overlay);
    lv_obj_set_size(box, 230, 130); 
    lv_obj_center(box);
    lv_obj_add_style(box, &style_box, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * info_cont = lv_obj_create(box);
    lv_obj_set_size(info_cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info_cont, 0, 0);
    lv_obj_set_style_border_width(info_cont, 0, 0);
    lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(info_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * icon_lbl = lv_label_create(info_cont);
    lv_label_set_text(icon_lbl, icon);
    lv_obj_set_style_text_color(icon_lbl, current_theme.border_accent, 0);

    lv_obj_t * msg_lbl = lv_label_create(info_cont);
    lv_label_set_text(msg_lbl, msg);
    lv_obj_set_style_text_color(msg_lbl, current_theme.text_main, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_lbl, 180);

    lv_obj_t * btn_cont = lv_obj_create(box);
    lv_obj_set_size(btn_cont, lv_pct(100), 45);
    lv_obj_set_style_bg_opa(btn_cont, 0, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_cont, 15, 0);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);

    bool has_ok = (btn_ok != NULL) && (btn_ok[0] != '\0');
    bool has_cancel = (btn_cancel != NULL) && (btn_cancel[0] != '\0');
    lv_obj_t * c_btn = NULL;
    lv_obj_t * o_btn = NULL;

    if(has_cancel) {
        c_btn = lv_btn_create(btn_cont);
        lv_obj_set_size(c_btn, 90, 32);
        lv_obj_add_style(c_btn, &style_btn, 0);
        lv_obj_set_style_border_color(c_btn, current_theme.border_accent, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_border_width(c_btn, 2, LV_STATE_FOCUS_KEY);
        
        lv_obj_t * c_lbl = lv_label_create(c_btn);
        lv_label_set_text(c_lbl, btn_cancel);
        lv_obj_center(c_lbl);
        lv_obj_add_event_cb(c_btn, btn_event_cb, LV_EVENT_ALL, (void*)false);
    }

    if(has_ok) {
        o_btn = lv_btn_create(btn_cont);
        lv_obj_set_size(o_btn, 90, 32);
        lv_obj_add_style(o_btn, &style_btn, 0);
        lv_obj_set_style_border_color(o_btn, current_theme.border_accent, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_border_width(o_btn, 2, LV_STATE_FOCUS_KEY);

        lv_obj_t * o_lbl = lv_label_create(o_btn);
        lv_label_set_text(o_lbl, btn_ok);
        lv_obj_center(o_lbl);
        lv_obj_add_event_cb(o_btn, btn_event_cb, LV_EVENT_ALL, (void*)true);
    }

    if(main_group) {
        lv_group_remove_all_objs(main_group);
        if(c_btn) lv_group_add_obj(main_group, c_btn);
        if(o_btn) {
            lv_group_add_obj(main_group, o_btn);
            lv_group_focus_obj(o_btn);
        }
        lv_group_set_editing(main_group, true);
    }
}

void msgbox_close(void) {
    if(msgbox_overlay) {
        if(main_group) {
            lv_group_set_editing(main_group, false);
            lv_group_remove_all_objs(main_group);
        }
        lv_obj_del_async(msgbox_overlay);
        msgbox_overlay = NULL;
        current_cb = NULL;
    }
}
