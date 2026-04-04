#include "msgbox_ui.h"
#include "ui_theme.h"
#include "assets_manager.h"
#include "buttons_gpio.h"
#include "st7789.h"

#define MSGBOX_H        ((LCD_V_RES * 45) / 100)
#define ANIM_TIME       300
#define BORDER_COLOR    current_theme.border_accent
#define GRAD_TOP        current_theme.border_interface
#define GRAD_BOT        current_theme.bg_secondary
#define BTN_W           80
#define BTN_H           28

static lv_obj_t * panel = NULL;
static lv_obj_t * btn_objs[2] = {NULL};
static int btn_count = 0;
static int btn_sel = 0;
static bool btn_confirms[2] = {false, false};
static msgbox_cb_t current_cb = NULL;
static lv_timer_t * msgbox_timer = NULL;

static bool btn_left_last = false;
static bool btn_right_last = false;
static bool btn_ok_last = false;
static bool btn_back_last = false;
static bool input_locked = true;

static void update_btn_selection(void) {
    for (int i = 0; i < btn_count; i++) {
        if (!btn_objs[i]) continue;
        if (i == btn_sel) {
            lv_obj_set_style_border_width(btn_objs[i], 2, 0);
            lv_obj_set_style_border_color(btn_objs[i], BORDER_COLOR, 0);
        } else {
            lv_obj_set_style_border_width(btn_objs[i], 1, 0);
            lv_obj_set_style_border_color(btn_objs[i], current_theme.border_interface, 0);
        }
    }
}

static void slide_anim_cb(void * var, int32_t val) {
    lv_obj_set_y((lv_obj_t *)var, val);
}

static void close_anim_done(lv_anim_t * a) {
    if (panel) {
        lv_obj_del(panel);
        panel = NULL;
    }
    btn_objs[0] = btn_objs[1] = NULL;
    btn_count = 0;
}

static void do_close(bool confirm) {
    if (!panel) return;

    if (current_cb) current_cb(confirm);
    current_cb = NULL;

    if (msgbox_timer) {
        lv_timer_delete(msgbox_timer);
        msgbox_timer = NULL;
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_values(&a, lv_obj_get_y(panel), LCD_V_RES);
    lv_anim_set_duration(&a, ANIM_TIME);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, slide_anim_cb);
    lv_anim_set_completed_cb(&a, close_anim_done);
    lv_anim_start(&a);
}

static void msgbox_timer_cb(lv_timer_t * t) {
    if (!panel || !lv_obj_is_valid(panel)) {
        lv_timer_delete(t);
        msgbox_timer = NULL;
        return;
    }

    bool left  = left_button_is_down();
    bool right = right_button_is_down();
    bool ok    = ok_button_is_down();
    bool back  = back_button_is_down();

    if (input_locked) {
        if (!ok && !back && !left && !right) input_locked = false;
        btn_left_last = left; btn_right_last = right;
        btn_ok_last = ok; btn_back_last = back;
        return;
    }

    if (left && !btn_left_last && btn_count > 1) {
        btn_sel = (btn_sel == 0) ? btn_count - 1 : btn_sel - 1;
        update_btn_selection();
    }
    if (right && !btn_right_last && btn_count > 1) {
        btn_sel = (btn_sel + 1) % btn_count;
        update_btn_selection();
    }
    if (ok && !btn_ok_last && btn_count > 0) {
        do_close(btn_confirms[btn_sel]);
    }
    if (back && !btn_back_last) {
        do_close(false);
    }

    btn_left_last = left; btn_right_last = right;
    btn_ok_last = ok; btn_back_last = back;
}

static lv_obj_t * create_btn(lv_obj_t * parent, const char * text) {
    lv_obj_t * btn = lv_obj_create(parent);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, GRAD_BOT, 0);
    lv_obj_set_style_bg_grad_color(btn, GRAD_TOP, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, current_theme.border_interface, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);

    return btn;
}

void msgbox_open(const char * icon, const char * msg, const char * btn_ok, const char * btn_cancel, msgbox_cb_t cb) {
    if (panel) msgbox_close();

    current_cb = cb;
    input_locked = true;
    btn_count = 0;
    btn_sel = 0;

    lv_obj_t * scr = lv_screen_active();
    panel = lv_obj_create(scr);
    lv_obj_set_size(panel, LCD_H_RES, MSGBOX_H);
    lv_obj_set_pos(panel, 0, LCD_V_RES);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, BORDER_COLOR, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, GRAD_BOT, 0);
    lv_obj_set_style_bg_grad_color(panel, GRAD_TOP, 0);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    lv_obj_move_foreground(panel);

    lv_obj_t * content = lv_obj_create(panel);
    lv_obj_set_size(content, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static lv_image_dsc_t * warn_dsc = NULL;
    if (!warn_dsc) warn_dsc = assets_get("/assets/icons/warning_icon.bin");
    if (warn_dsc) {
        lv_obj_t * icon_img = lv_image_create(content);
        lv_image_set_src(icon_img, warn_dsc);
    }

    lv_obj_t * msg_lbl = lv_label_create(content);
    lv_label_set_text(msg_lbl, msg ? msg : "");
    lv_obj_set_style_text_color(msg_lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_lbl, LCD_H_RES - 40);

    lv_obj_t * btn_row = lv_obj_create(content);
    lv_obj_set_size(btn_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_column(btn_row, 15, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    bool has_cancel = btn_cancel && btn_cancel[0];
    bool has_ok = btn_ok && btn_ok[0];

    if (has_cancel) {
        btn_objs[btn_count] = create_btn(btn_row, btn_cancel);
        btn_confirms[btn_count] = false;
        btn_count++;
    }
    if (has_ok) {
        btn_objs[btn_count] = create_btn(btn_row, btn_ok);
        btn_confirms[btn_count] = true;
        btn_sel = btn_count;
        btn_count++;
    }

    update_btn_selection();

    int target_y = LCD_V_RES - MSGBOX_H;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_values(&a, LCD_V_RES, target_y);
    lv_anim_set_duration(&a, ANIM_TIME);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, slide_anim_cb);
    lv_anim_start(&a);

    if (!msgbox_timer)
        msgbox_timer = lv_timer_create(msgbox_timer_cb, 50, NULL);
}

void msgbox_close(void) {
    if (panel) {
        do_close(false);
    }
}

bool msgbox_is_open(void) {
    return (panel != NULL);
}
