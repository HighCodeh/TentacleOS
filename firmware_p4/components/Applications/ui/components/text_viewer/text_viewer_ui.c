#include "text_viewer_ui.h"
#include "assets_manager.h"
#include "ui_theme.h"
#include "st7789.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BORDER_COLOR    current_theme.border_interface
#define ITEM_BORDER     current_theme.border_accent
#define GRAD_LEFT       current_theme.bg_primary
#define GRAD_RIGHT      current_theme.bg_secondary
#define OUTER_BORDER    4
#define TOP_BORDER_H    46

static text_viewer_t * active_viewer = NULL;

static void scroll_event_cb(lv_event_t * e) {
    if (!active_viewer || !active_viewer->scroll_bar) return;
    lv_obj_t * area = lv_event_get_target(e);

    int32_t scroll_y = lv_obj_get_scroll_y(area);
    int32_t scroll_max = lv_obj_get_scroll_bottom(area) + scroll_y;
    if (scroll_max <= 0) return;

    int32_t pct = (scroll_y * 100) / scroll_max;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    int32_t bar_y = active_viewer->track_y_start + (pct * (active_viewer->track_h - 20)) / 100;
    lv_obj_set_y(active_viewer->scroll_bar, bar_y);
}

text_viewer_t text_viewer_create(lv_obj_t * parent, const char * filename)
{
    text_viewer_t tv = {0};

    tv.screen = lv_obj_create(parent);
    lv_obj_set_size(tv.screen, LCD_H_RES, LCD_V_RES);
    lv_obj_align(tv.screen, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(tv.screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tv.screen, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(tv.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(tv.screen, 0, 0);
    lv_obj_set_style_border_width(tv.screen, OUTER_BORDER, 0);
    lv_obj_set_style_border_color(tv.screen, BORDER_COLOR, 0);
    lv_obj_set_style_radius(tv.screen, 0, 0);

    lv_obj_t * top_area = lv_obj_create(tv.screen);
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
    lv_obj_set_size(title_bar, 190, 30);
    lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_bar, 12, 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(title_bar, GRAD_LEFT, 0);
    lv_obj_set_style_bg_grad_color(title_bar, GRAD_RIGHT, 0);
    lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(title_bar, 2, 0);
    lv_obj_set_style_border_color(title_bar, ITEM_BORDER, 0);

    tv.title_label = lv_label_create(title_bar);
    lv_label_set_text(tv.title_label, filename ? filename : "File");
    lv_obj_set_style_text_color(tv.title_label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(tv.title_label, &lv_font_montserrat_12, 0);
    lv_obj_center(tv.title_label);

    tv.line_label = lv_label_create(tv.screen);
    lv_label_set_text(tv.line_label, "");
    lv_obj_set_style_text_color(tv.line_label, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(tv.line_label, &lv_font_montserrat_12, 0);
    lv_obj_align(tv.line_label, LV_ALIGN_TOP_MID, 0, TOP_BORDER_H + 2);

    int content_y = TOP_BORDER_H + 18;
    int content_h = LCD_V_RES - content_y - OUTER_BORDER - 4;

    tv.text_area = lv_obj_create(tv.screen);
    lv_obj_set_size(tv.text_area, LCD_H_RES - OUTER_BORDER * 2 - 16, content_h);
    lv_obj_align(tv.text_area, LV_ALIGN_TOP_LEFT, OUTER_BORDER + 4, content_y);
    lv_obj_set_style_bg_opa(tv.text_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tv.text_area, 0, 0);
    lv_obj_set_style_pad_all(tv.text_area, 6, 0);
    lv_obj_set_scrollbar_mode(tv.text_area, LV_SCROLLBAR_MODE_OFF);

    int track_x = LCD_H_RES - OUTER_BORDER - 10;
    tv.track_y_start = content_y + 10;
    tv.track_h = content_h - 20;

    static lv_point_precise_t track_pts[2];
    track_pts[0].x = 0; track_pts[0].y = 0;
    track_pts[1].x = 0; track_pts[1].y = tv.track_h;

    lv_obj_t * track = lv_line_create(tv.screen);
    lv_line_set_points(track, track_pts, 2);
    lv_obj_set_pos(track, track_x, tv.track_y_start);
    lv_obj_set_style_line_color(track, current_theme.text_main, 0);
    lv_obj_set_style_line_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_line_width(track, 3, 0);
    lv_obj_set_style_line_dash_width(track, 4, 0);
    lv_obj_set_style_line_dash_gap(track, 4, 0);

    static lv_image_dsc_t * sb_dsc = NULL;
    if (!sb_dsc) sb_dsc = assets_get("/assets/icons/slide_bar_v.bin");
    tv.scroll_bar = lv_image_create(tv.screen);
    if (sb_dsc) lv_image_set_src(tv.scroll_bar, sb_dsc);
    lv_obj_set_pos(tv.scroll_bar, track_x - 4, tv.track_y_start);
    lv_obj_move_foreground(tv.scroll_bar);

    lv_obj_add_event_cb(tv.text_area, scroll_event_cb, LV_EVENT_SCROLL, NULL);

    return tv;
}

void text_viewer_load_file(text_viewer_t * tv, const char * path)
{
    if (!tv || !tv->text_area || !path) return;

    FILE * f = fopen(path, "r");
    if (!f) {
        lv_obj_t * lbl = lv_label_create(tv->text_area);
        lv_label_set_text(lbl, "Failed to open file");
        lv_obj_set_style_text_color(lbl, current_theme.border_accent, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > 4096) size = 4096;

    char * buf = malloc(size + 1);
    if (!buf) { fclose(f); return; }

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    text_viewer_set_text(tv, buf);
    free(buf);
}

void text_viewer_set_text(text_viewer_t * tv, const char * text)
{
    if (!tv || !tv->text_area) return;

    lv_obj_clean(tv->text_area);

    lv_obj_t * lbl = lv_label_create(tv->text_area);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_width(lbl, LCD_H_RES - OUTER_BORDER * 2 - 36);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);

    if (tv->line_label && text) {
        int lines = 1;
        int len = 0;
        for (const char * p = text; *p; p++) {
            if (*p == '\n') lines++;
            len++;
        }
        if (len < 1024) {
            lv_label_set_text_fmt(tv->line_label, "%d lines  |  %d bytes", lines, len);
        } else {
            lv_label_set_text_fmt(tv->line_label, "%d lines  |  %.1f KB", lines, len / 1024.0f);
        }
    }

    active_viewer = tv;
}
