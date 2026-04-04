// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ir_burst_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "spinner_ui.h"
#include "msgbox_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "buttons_gpio.h"
#include "ir.h"
#include "ir_file.h"
#include "tos_storage_paths.h"
#include "st7789.h"

static const char *TAG = "IR_BURST_UI";

#define OUTER_BORDER     4
#define TOP_BORDER_H     46
#define BURST_DELAY_MS   150

static lv_obj_t     *s_screen       = NULL;
static lv_timer_t   *s_nav_timer    = NULL;
static lv_obj_t     *s_status_label = NULL;
static lv_obj_t     *s_count_label  = NULL;
static spinner_ui_t  s_spinner;

static bool s_btn_back_last = false;

static TaskHandle_t   s_burst_task   = NULL;
static volatile bool  s_is_done      = false;
static volatile bool  s_stop_requested = false;
static volatile int   s_sent_count   = 0;
static volatile int   s_total_count  = 0;

/* ── Send one .ir file ───────────────────────────────────────── */
static void send_one_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4096) { fclose(f); return; }

    char *buf = malloc(sz + 1);
    if (buf == NULL) { fclose(f); return; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    ir_file_t ir_file;
    ir_file_init(&ir_file);

    if (ir_file_parse(buf, &ir_file)) {
        for (size_t i = 0; i < ir_file.count && !s_stop_requested; i++) {
            ir_file_send(&ir_file.signals[i]);
            vTaskDelay(pdMS_TO_TICKS(BURST_DELAY_MS));
        }
        s_sent_count++;
    }

    ir_file_free(&ir_file);
    free(buf);
}

/* ── Burst task: scan protocol subdirs, send all .ir files ───── */
static void burst_task(void *pvParameters) {
    (void)pvParameters;
    ir_tx_init();

    DIR *root = opendir(TOS_PATH_IR);
    if (root == NULL) {
        s_is_done = true;
        s_burst_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Count total files across all protocol dirs */
    struct dirent *proto_ent;
    int total = 0;
    while ((proto_ent = readdir(root)) != NULL) {
        if (proto_ent->d_name[0] == '.' || proto_ent->d_type != DT_DIR) continue;
        char sub_path[512];
        snprintf(sub_path, sizeof(sub_path), TOS_PATH_IR "/%.64s", proto_ent->d_name);
        DIR *sub = opendir(sub_path);
        if (sub == NULL) continue;
        struct dirent *fe;
        while ((fe = readdir(sub)) != NULL) {
            size_t len = strlen(fe->d_name);
            if (len >= 4 && strcmp(fe->d_name + len - 3, ".ir") == 0) total++;
        }
        closedir(sub);
    }
    s_total_count = total;
    rewinddir(root);

    /* Send all files */
    while ((proto_ent = readdir(root)) != NULL && !s_stop_requested) {
        if (proto_ent->d_name[0] == '.' || proto_ent->d_type != DT_DIR) continue;
        char sub_path[512];
        snprintf(sub_path, sizeof(sub_path), TOS_PATH_IR "/%.64s", proto_ent->d_name);
        DIR *sub = opendir(sub_path);
        if (sub == NULL) continue;
        struct dirent *fe;
        while ((fe = readdir(sub)) != NULL && !s_stop_requested) {
            size_t len = strlen(fe->d_name);
            if (len < 4 || strcmp(fe->d_name + len - 3, ".ir") != 0) continue;
            char file_path[600];
            snprintf(file_path, sizeof(file_path), "%.299s/%.255s", sub_path, fe->d_name);
            send_one_file(file_path);
        }
        closedir(sub);
    }

    closedir(root);
    s_is_done = true;
    s_burst_task = NULL;
    vTaskDelete(NULL);
}

/* ── Navigation ──────────────────────────────────────────────── */
static void nav_timer_cb(lv_timer_t *t) {
    if (lv_screen_active() != s_screen) {
        lv_timer_delete(t);
        s_nav_timer = NULL;
        return;
    }
    if (ui_input_is_locked()) return;
    if (msgbox_is_open()) return;

    /* Update progress */
    if (!s_is_done && s_total_count > 0) {
        lv_label_set_text_fmt(s_count_label, "%d / %d files", s_sent_count, s_total_count);
    }

    if (s_is_done) {
        s_is_done = false;
        spinner_ui_hide(&s_spinner);
        lv_label_set_text(s_status_label, "Burst complete!");
        lv_label_set_text_fmt(s_count_label, "%d signals sent", s_sent_count);
    }

    bool back = back_button_is_down();

    if (back && !s_btn_back_last) {
        s_stop_requested = true;
        if (s_burst_task != NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (s_burst_task != NULL) {
                vTaskDelete(s_burst_task);
                s_burst_task = NULL;
            }
        }
        ui_switch_screen(SCREEN_IR_MENU);
    }

    s_btn_back_last = back;
}

/* ── Screen open ─────────────────────────────────────────────── */
void ui_ir_burst_open(void) {
    if (s_screen != NULL) { lv_obj_del(s_screen); s_screen = NULL; }

    s_is_done = false;
    s_stop_requested = false;
    s_sent_count = 0;
    s_total_count = 0;
    s_burst_task = NULL;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_screen, OUTER_BORDER, 0);
    lv_obj_set_style_border_color(s_screen, current_theme.border_interface, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    /* Top area */
    lv_obj_t *top_area = lv_obj_create(s_screen);
    lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
    lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_area, 3, 0);
    lv_obj_set_style_border_color(top_area, current_theme.border_interface, 0);
    lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(top_area, 0, 0);
    lv_obj_set_style_pad_all(top_area, 0, 0);

    /* Title pill */
    lv_obj_t *title_bar = lv_obj_create(top_area);
    lv_obj_set_size(title_bar, 170, 30);
    lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_bar, 12, 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(title_bar, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(title_bar, 2, 0);
    lv_obj_set_style_border_color(title_bar, current_theme.border_accent, 0);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, "IR BURST");
    lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(title_lbl);

    /* Status */
    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "Burst running...");
    lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, -10);

    s_count_label = lv_label_create(s_screen);
    lv_label_set_text(s_count_label, "Scanning files...");
    lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_count_label, LV_ALIGN_CENTER, 0, 15);

    s_spinner = spinner_ui_create(s_screen, 30);
    lv_obj_align(s_spinner.obj, LV_ALIGN_BOTTOM_MID, 0, -20);

    if (s_nav_timer == NULL) {
        s_nav_timer = lv_timer_create(nav_timer_cb, 50, NULL);
    }

    xTaskCreate(burst_task, "ir_burst", 8192, NULL, 5, &s_burst_task);

    lv_screen_load(s_screen);
}
