#include "area_chart_ui.h"
#include "ui_theme.h"
#include "st7789.h"

#define BORDER_COLOR    current_theme.border_interface
#define ITEM_BORDER     current_theme.border_accent
#define GRAD_TOP        current_theme.border_interface
#define GRAD_BOT        current_theme.bg_secondary
#define LINE_COLOR      current_theme.border_accent
#define AREA_TOP_COLOR  current_theme.border_accent
#define AREA_BOT_COLOR  current_theme.border_accent
#define BTN_BG          current_theme.border_interface
#define BTN_GRAD        current_theme.border_accent
#define SEL_BORDER      current_theme.border_accent

#define OUTER_BORDER    4
#define PANEL_H         80
#define BTN_W           90
#define BTN_H           32

static void update_btn_sel(area_chart_ui_t * ch) {
    for (int i = 0; i < ch->btn_count; i++) {
        if (!ch->btns[i]) continue;
        if (i == ch->btn_sel) {
            lv_obj_set_style_border_width(ch->btns[i], 2, 0);
            lv_obj_set_style_border_color(ch->btns[i], SEL_BORDER, 0);
        } else {
            lv_obj_set_style_border_width(ch->btns[i], 1, 0);
            lv_obj_set_style_border_color(ch->btns[i], BORDER_COLOR, 0);
        }
    }
}


area_chart_ui_t area_chart_ui_create(lv_obj_t * parent)
{
    area_chart_ui_t ch = {0};

    ch.screen = lv_obj_create(parent);
    lv_obj_set_size(ch.screen, LCD_H_RES, LCD_V_RES);
    lv_obj_align(ch.screen, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(ch.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ch.screen, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(ch.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ch.screen, 0, 0);
    lv_obj_set_style_border_width(ch.screen, 0, 0);

    int chart_y = OUTER_BORDER;
    int chart_h = LCD_V_RES - PANEL_H - OUTER_BORDER * 2 - 4;

    ch.chart = lv_chart_create(ch.screen);
    lv_obj_set_size(ch.chart, LCD_H_RES - OUTER_BORDER * 2, chart_h);
    lv_obj_set_pos(ch.chart, OUTER_BORDER, chart_y);
    lv_obj_remove_flag(ch.chart, LV_OBJ_FLAG_SCROLLABLE);

    lv_chart_set_type(ch.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch.chart, AREA_CHART_MAX_POINTS);
    lv_chart_set_range(ch.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(ch.chart, 4, 0);

    lv_obj_set_style_bg_color(ch.chart, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(ch.chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ch.chart, 1, 0);
    lv_obj_set_style_border_color(ch.chart, BORDER_COLOR, 0);
    lv_obj_set_style_radius(ch.chart, 0, 0);
    lv_obj_set_style_pad_all(ch.chart, 8, 0);
    lv_obj_set_style_line_color(ch.chart, current_theme.bg_item_bot, 0);
    lv_obj_set_style_line_width(ch.chart, 1, 0);

    lv_obj_set_style_line_color(ch.chart, LINE_COLOR, LV_PART_ITEMS);
    lv_obj_set_style_line_width(ch.chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(ch.chart, 0, 0, LV_PART_INDICATOR);

    ch.series = lv_chart_add_series(ch.chart, LINE_COLOR, LV_CHART_AXIS_PRIMARY_Y);

    for (int i = 0; i < AREA_CHART_MAX_POINTS; i++) {
        lv_chart_set_next_value(ch.chart, ch.series, 0);
    }

    ch.panel = lv_obj_create(ch.screen);
    lv_obj_set_size(ch.panel, LCD_H_RES - OUTER_BORDER * 2, PANEL_H);
    lv_obj_align(ch.panel, LV_ALIGN_BOTTOM_MID, 0, -OUTER_BORDER);
    lv_obj_remove_flag(ch.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ch.panel, 12, 0);
    lv_obj_set_style_bg_opa(ch.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ch.panel, GRAD_BOT, 0);
    lv_obj_set_style_bg_grad_color(ch.panel, GRAD_TOP, 0);
    lv_obj_set_style_bg_grad_dir(ch.panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(ch.panel, 2, 0);
    lv_obj_set_style_border_color(ch.panel, ITEM_BORDER, 0);
    lv_obj_set_style_pad_all(ch.panel, 10, 0);
    lv_obj_set_flex_flow(ch.panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ch.panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    ch.btn_count = 0;
    ch.btn_sel = 0;
    ch.btn_cb = NULL;

    return ch;
}

void area_chart_ui_add_btn(area_chart_ui_t * ch, const char * label, lv_color_t dot_color)
{
    if (!ch || ch->btn_count >= 2) return;
    int idx = ch->btn_count;

    lv_obj_t * btn = lv_obj_create(ch->panel);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(btn, 17, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, BTN_BG, 0);
    lv_obj_set_style_bg_grad_color(btn, BTN_GRAD, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, BORDER_COLOR, 0);
    lv_obj_set_style_pad_left(btn, 10, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn, 8, 0);

    lv_obj_t * dot = lv_obj_create(btn);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, dot_color, 0);
    lv_obj_set_style_border_width(dot, 0, 0);

    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label ? label : "");
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

    ch->btns[idx] = btn;
    ch->btn_count++;
    update_btn_sel(ch);
}

void area_chart_ui_set_btn_cb(area_chart_ui_t * ch, area_chart_btn_cb_t cb) {
    if (ch) ch->btn_cb = cb;
}

void area_chart_ui_set_btn_dim(area_chart_ui_t * ch, int index, bool dim) {
    if (!ch || index < 0 || index >= ch->btn_count || !ch->btns[index]) return;
    lv_obj_set_style_opa(ch->btns[index], dim ? LV_OPA_50 : LV_OPA_COVER, 0);
}

void area_chart_ui_add_point(area_chart_ui_t * ch, int32_t value) {
    if (!ch || !ch->chart || !ch->series) return;
    lv_chart_set_next_value(ch->chart, ch->series, value);
    lv_chart_refresh(ch->chart);
}

void area_chart_ui_set_points(area_chart_ui_t * ch, const int32_t * values, int count) {
    if (!ch || !ch->chart || !ch->series) return;
    for (int i = 0; i < count && i < AREA_CHART_MAX_POINTS; i++) {
        lv_chart_set_next_value(ch->chart, ch->series, values[i]);
    }
    lv_chart_refresh(ch->chart);
}

void area_chart_ui_btn_next(area_chart_ui_t * ch) {
    if (!ch || ch->btn_count <= 1) return;
    ch->btn_sel = (ch->btn_sel + 1) % ch->btn_count;
    update_btn_sel(ch);
}

void area_chart_ui_btn_prev(area_chart_ui_t * ch) {
    if (!ch || ch->btn_count <= 1) return;
    ch->btn_sel = (ch->btn_sel == 0) ? ch->btn_count - 1 : ch->btn_sel - 1;
    update_btn_sel(ch);
}

void area_chart_ui_btn_press(area_chart_ui_t * ch) {
    if (!ch || ch->btn_count == 0) return;
    if (ch->btn_cb) ch->btn_cb(ch->btn_sel);
}
