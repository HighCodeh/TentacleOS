#ifndef UI_INTENSITY_BAR_H
#define UI_INTENSITY_BAR_H

#include "lvgl.h"

#define INTENSITY_BAR_STEPS 5

typedef struct {
    lv_obj_t * obj;
    lv_obj_t * bars[INTENSITY_BAR_STEPS];
    int level;
} intensity_bar_t;

void intensity_bar_create(intensity_bar_t * ib, lv_obj_t * parent);
void intensity_bar_set(intensity_bar_t * ib, int level);
int  intensity_bar_get(intensity_bar_t * ib);
void intensity_bar_inc(intensity_bar_t * ib);
void intensity_bar_dec(intensity_bar_t * ib);

#endif
