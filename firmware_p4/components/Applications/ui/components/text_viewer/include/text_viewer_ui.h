#ifndef UI_TEXT_VIEWER_H
#define UI_TEXT_VIEWER_H

#include "lvgl.h"

typedef struct {
    lv_obj_t * screen;
    lv_obj_t * title_label;
    lv_obj_t * line_label;
    lv_obj_t * text_area;
    lv_obj_t * scroll_bar;
    int track_y_start;
    int track_h;
} text_viewer_t;

/* Create a full-screen text viewer with title and scrollable content */
text_viewer_t text_viewer_create(lv_obj_t * parent, const char * filename);

/* Load text content from a file path */
void text_viewer_load_file(text_viewer_t * tv, const char * path);

/* Set text content directly */
void text_viewer_set_text(text_viewer_t * tv, const char * text);

#endif
