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
#define BOOT_LABEL_OFFSET_Y        (-10)
#define BOOT_TEXT_ANIM_DURATION_MS 10000
#define BOOT_TEXT_ANIM_END_VAL     30
#define BOOT_FADE_HOLD_MS          2600
#define BOOT_BG_COLOR              0x000000
#define BOOT_TEXT_COLOR            0xFFFFFF

static lv_image_dsc_t *s_octo1_dsc = NULL;
static lv_image_dsc_t *s_octo2_dsc = NULL;

static void anim_set_opa_cb(void *var, int32_t v);
static void boot_text_anim_cb(void *var, int32_t v);
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
}

static void anim_set_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void boot_text_anim_cb(void *var, int32_t v) {
  const char *dots[] = {"Booting.", "Booting..", "Booting..."};
  lv_label_set_text((lv_obj_t *)var, dots[v % 3]);
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