// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "boot_ui.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

#include "assets_manager.h"

static const char *TAG = "UI_BOOT";

#define BOOT_OCTO2_DELAY_MS        200
#define BOOT_OCTO2_FADE_IN_MS      400
#define BOOT_OCTO2_FADE_OUT_MS     400
#define BOOT_OCTO1_DELAY_MS        3400
#define BOOT_OCTO1_FADE_IN_MS      500
#define BOOT_LABEL_OFFSET_Y        (-48)
#define BOOT_TEXT_ANIM_DURATION_MS 10000
#define BOOT_TEXT_ANIM_END_VAL     30
#define BOOT_FADE_HOLD_MS          2600
#define BOOT_BG_COLOR              0x000000
#define BOOT_TEXT_COLOR            0xFFFFFF
#define BOOT_LOG_COLOR             0x00E676
#define BOOT_LOG_STEP_MS           220
#define BOOT_LOG_OFFSET_Y          (-26)
#define BOOT_LOG_BUF_SIZE          64

static lv_image_dsc_t *s_octo1_dsc = NULL;
static lv_image_dsc_t *s_octo2_dsc = NULL;

static lv_obj_t *s_boot_screen = NULL;
static lv_obj_t *s_log_label = NULL;
static uint32_t s_log_index = 0;

static const char *s_boot_log_steps[] = {"st7789 display",
                                         "lvgl 9.4",
                                         "buttons",
                                         "i2s audio",
                                         "led rgb",
                                         "sd card",
                                         "haptics",
                                         "console",
                                         "c5 bridge",
                                         "ui manager"};

static void anim_set_opa_cb(void *var, int32_t v);
static void boot_text_anim_cb(void *var, int32_t v);
static void boot_log_timer_cb(lv_timer_t *t);
static lv_obj_t *create_fade_image(
    lv_obj_t *parent, lv_image_dsc_t *src, uint32_t delay, uint32_t fade_in, uint32_t fade_out);

void ui_boot_show(void) {
  lv_obj_t *boot_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(boot_screen, lv_color_hex(BOOT_BG_COLOR), 0);
  lv_obj_set_style_bg_opa(boot_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clean(boot_screen);
  lv_screen_load(boot_screen);
  lv_refr_now(NULL);

  s_octo2_dsc = assets_get("/assets/img/octobit_boot_2.bin");
  s_octo1_dsc = assets_get("/assets/img/octobit_boot_1.bin");

  if (s_octo2_dsc != NULL)
    create_fade_image(boot_screen,
                      s_octo2_dsc,
                      BOOT_OCTO2_DELAY_MS,
                      BOOT_OCTO2_FADE_IN_MS,
                      BOOT_OCTO2_FADE_OUT_MS);

  if (s_octo1_dsc != NULL)
    create_fade_image(boot_screen, s_octo1_dsc, BOOT_OCTO1_DELAY_MS, BOOT_OCTO1_FADE_IN_MS, 0);

  lv_obj_t *boot_label = lv_label_create(boot_screen);
  lv_label_set_text(boot_label, "Booting.");
  lv_obj_set_style_text_color(boot_label, lv_color_hex(BOOT_TEXT_COLOR), 0);
  lv_obj_set_style_text_font(boot_label, &lv_font_montserrat_14, 0);
  lv_obj_align(boot_label, LV_ALIGN_BOTTOM_MID, 0, BOOT_LABEL_OFFSET_Y);

  lv_anim_t a_text;
  lv_anim_init(&a_text);
  lv_anim_set_var(&a_text, boot_label);
  lv_anim_set_values(&a_text, 0, BOOT_TEXT_ANIM_END_VAL);
  lv_anim_set_duration(&a_text, BOOT_TEXT_ANIM_DURATION_MS);
  lv_anim_set_exec_cb(&a_text, boot_text_anim_cb);
  lv_anim_set_repeat_count(&a_text, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a_text);

  s_boot_screen = boot_screen;
  s_log_index = 0;
  s_log_label = lv_label_create(boot_screen);
  lv_label_set_text(s_log_label, "");
  lv_obj_set_style_text_color(s_log_label, lv_color_hex(BOOT_LOG_COLOR), 0);
  lv_obj_set_style_text_font(s_log_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_log_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_log_label, LV_ALIGN_BOTTOM_MID, 0, BOOT_LOG_OFFSET_Y);
  lv_timer_create(boot_log_timer_cb, BOOT_LOG_STEP_MS, NULL);
}

static void anim_set_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void boot_text_anim_cb(void *var, int32_t v) {
  const char *dots[] = {"Booting.", "Booting..", "Booting..."};
  lv_label_set_text((lv_obj_t *)var, dots[v % 3]);
}

static void boot_log_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_boot_screen || s_log_label == NULL) {
    lv_timer_delete(t);
    return;
  }

  uint32_t count = sizeof(s_boot_log_steps) / sizeof(s_boot_log_steps[0]);
  char buf[BOOT_LOG_BUF_SIZE];
  snprintf(buf, sizeof(buf), "> %s", s_boot_log_steps[s_log_index % count]);
  lv_label_set_text(s_log_label, buf);
  s_log_index++;
}

static lv_obj_t *create_fade_image(
    lv_obj_t *parent, lv_image_dsc_t *src, uint32_t delay, uint32_t fade_in, uint32_t fade_out) {
  if (src == NULL)
    return NULL;

  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, src);
  lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_opa(img, 0, 0);
  lv_obj_invalidate(img);

  lv_anim_t a_in;
  lv_anim_init(&a_in);
  lv_anim_set_var(&a_in, img);
  lv_anim_set_values(&a_in, 0, 255);
  lv_anim_set_delay(&a_in, delay);
  lv_anim_set_duration(&a_in, fade_in);
  lv_anim_set_exec_cb(&a_in, anim_set_opa_cb);
  lv_anim_start(&a_in);

  if (fade_out > 0) {
    lv_anim_t a_out;
    lv_anim_init(&a_out);
    lv_anim_set_var(&a_out, img);
    lv_anim_set_values(&a_out, 255, 0);
    lv_anim_set_delay(&a_out, delay + fade_in + BOOT_FADE_HOLD_MS);
    lv_anim_set_duration(&a_out, fade_out);
    lv_anim_set_exec_cb(&a_out, anim_set_opa_cb);
    lv_anim_set_ready_cb(&a_out, lv_obj_delete_anim_ready_cb);
    lv_anim_start(&a_out);
  }

  return img;
}