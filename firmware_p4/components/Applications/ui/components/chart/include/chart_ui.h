#ifndef UI_CHART_H
#define UI_CHART_H

#include "lvgl.h"

#define CHART_MAX_POINTS 20

typedef void (*chart_btn_cb_t)(int btn_index);

typedef struct {
    lv_obj_t * screen;
    lv_obj_t * chart;
    lv_chart_series_t * series;
    lv_obj_t * panel;
    lv_obj_t * btns[2];
    lv_obj_t * btn_dots[2];
    int btn_count;
    int btn_sel;
    chart_btn_cb_t btn_cb;
} chart_ui_t;

/* Create chart screen with header, bar chart, and bottom panel */
chart_ui_t chart_ui_create(lv_obj_t * parent, const char * title);

/* Add a button to bottom panel (max 2). dot_color: red/green circle */
void chart_ui_add_btn(chart_ui_t * ch, const char * label, lv_color_t dot_color);

/* Set button callback */
void chart_ui_set_btn_cb(chart_ui_t * ch, chart_btn_cb_t cb);

/* Add a data point to the chart */
void chart_ui_add_point(chart_ui_t * ch, int32_t value);

/* Set all points at once */
void chart_ui_set_points(chart_ui_t * ch, const int32_t * values, int count);

/* Dim/undim a button (visual inactive state) */
void chart_ui_set_btn_dim(chart_ui_t * ch, int index, bool dim);

/* Navigation */
void chart_ui_btn_next(chart_ui_t * ch);
void chart_ui_btn_prev(chart_ui_t * ch);
void chart_ui_btn_press(chart_ui_t * ch);

#endif
