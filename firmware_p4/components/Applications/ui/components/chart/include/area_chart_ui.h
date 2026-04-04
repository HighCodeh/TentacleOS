#ifndef UI_AREA_CHART_H
#define UI_AREA_CHART_H

#include "lvgl.h"

#define AREA_CHART_MAX_POINTS 40

typedef void (*area_chart_btn_cb_t)(int btn_index);

typedef struct {
    lv_obj_t * screen;
    lv_obj_t * chart;
    lv_chart_series_t * series;
    lv_obj_t * panel;
    lv_obj_t * btns[2];
    int btn_count;
    int btn_sel;
    area_chart_btn_cb_t btn_cb;
} area_chart_ui_t;

area_chart_ui_t area_chart_ui_create(lv_obj_t * parent);

void area_chart_ui_add_btn(area_chart_ui_t * ch, const char * label, lv_color_t dot_color);
void area_chart_ui_set_btn_cb(area_chart_ui_t * ch, area_chart_btn_cb_t cb);
void area_chart_ui_set_btn_dim(area_chart_ui_t * ch, int index, bool dim);

void area_chart_ui_add_point(area_chart_ui_t * ch, int32_t value);
void area_chart_ui_set_points(area_chart_ui_t * ch, const int32_t * values, int count);

void area_chart_ui_btn_next(area_chart_ui_t * ch);
void area_chart_ui_btn_prev(area_chart_ui_t * ch);
void area_chart_ui_btn_press(area_chart_ui_t * ch);

#endif
