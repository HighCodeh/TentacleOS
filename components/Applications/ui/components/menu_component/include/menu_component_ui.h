#ifndef UI_MENU_COMPONENT_H
#define UI_MENU_COMPONENT_H

#include "lvgl.h"

typedef struct {
    lv_obj_t *container;
} menu_component_t;

menu_component_t menu_component_create(lv_obj_t * parent,
                                       lv_coord_t width,
                                       lv_coord_t height,
                                       lv_align_t align,
                                       lv_coord_t x_ofs,
                                       lv_coord_t y_ofs);

lv_obj_t * menu_component_add_item(menu_component_t * menu,
                                   const char * symbol,
                                   const char * label);

lv_obj_t * menu_component_add_nav_item(menu_component_t * menu,
                                       const char * symbol,
                                       const char * label,
                                       lv_obj_t ** out_arrow_label);

lv_obj_t * menu_component_add_value_item(menu_component_t * menu,
                                         const char * symbol,
                                         const char * label,
                                         const char * value,
                                         lv_obj_t ** out_value_label);

lv_obj_t * menu_component_add_switch_item(menu_component_t * menu,
                                          const char * symbol,
                                          const char * label,
                                          lv_obj_t ** out_switch_container);

#endif
