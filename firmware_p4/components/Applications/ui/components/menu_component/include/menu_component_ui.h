#ifndef UI_MENU_COMPONENT_H
#define UI_MENU_COMPONENT_H

#include "lvgl.h"
#include "toggle_ui.h"
#include "intensity_bar_ui.h"

#define MENU_COMP_MAX_ITEMS 12

typedef struct {
    lv_obj_t * screen;
    lv_obj_t * title_bar;
    lv_obj_t * title_label;
    lv_obj_t * items_cont;
    lv_obj_t * items[MENU_COMP_MAX_ITEMS];
    lv_obj_t * scroll_bar;
    lv_obj_t * sel_dots[MENU_COMP_MAX_ITEMS];
    lv_obj_t * val_labels[MENU_COMP_MAX_ITEMS];
    toggle_ui_t toggles[MENU_COMP_MAX_ITEMS];
    bool has_toggle[MENU_COMP_MAX_ITEMS];
    intensity_bar_t intensities[MENU_COMP_MAX_ITEMS];
    bool has_intensity[MENU_COMP_MAX_ITEMS];
    int track_y_start;
    int track_h;
    int item_count;
    int selected;
} menu_component_t;

/* Create the full menu screen layout (borders, title, items area, scrollbar) */
menu_component_t menu_component_create(lv_obj_t * parent, const char * title, const char * title_icon_path);

/* Add items — returns the item obj for customization */
lv_obj_t * menu_component_add_item(menu_component_t * menu,
                                   const char * icon_path,
                                   const char * label);

/* Selector: shows "< value >" on the right, returns value label for updates */
lv_obj_t * menu_component_add_selector(menu_component_t * menu,
                                       const char * icon_path,
                                       const char * label,
                                       const char * initial_value);

/* Update selector value text */
void menu_component_set_selector_value(menu_component_t * menu, int index, const char * value);

/* Toggle: ON/OFF switch on the right side */
lv_obj_t * menu_component_add_toggle(menu_component_t * menu,
                                     const char * icon_path,
                                     const char * label,
                                     bool initial_state);

/* Toggle control */
void menu_component_toggle_item(menu_component_t * menu, int index);
bool menu_component_get_toggle(menu_component_t * menu, int index);
void menu_component_set_toggle(menu_component_t * menu, int index, bool state);

/* Intensity bar: 5 bars for volume/brightness */
lv_obj_t * menu_component_add_intensity(menu_component_t * menu,
                                        const char * icon_path,
                                        const char * label,
                                        int initial_level);

/* Intensity control */
void menu_component_intensity_inc(menu_component_t * menu, int index);
void menu_component_intensity_dec(menu_component_t * menu, int index);
int  menu_component_get_intensity(menu_component_t * menu, int index);

/* Navigation */
void menu_component_select(menu_component_t * menu, int index);
void menu_component_next(menu_component_t * menu);
void menu_component_prev(menu_component_t * menu);
int  menu_component_get_selected(menu_component_t * menu);

#endif
