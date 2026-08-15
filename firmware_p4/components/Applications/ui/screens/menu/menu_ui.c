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

#include "menu_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "home_ui.h"
#include "header_ui.h"
#include "ui_feedback.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "assets_manager.h"
#include "page_dots_ui.h"
#include "st7789.h"

static const char *TAG = "UI_MENU";

#define MENU_ITEM_FRAME_COUNT 3
#define MENU_ITEM_COUNT       (sizeof(s_menu_data) / sizeof(s_menu_data[0]))
#define ANIM_MS               400
#define LABEL_FADE_MS         300
#define FONT_PATH             "A:assets/fonts/Inter.bin"
#define ICON_CENTER_OFFSET_Y  (-10)
#define LABEL_OFFSET_Y        (-40)
#define DOTS_OFFSET_Y         (-20)

static const int32_t CAROUSEL_PX[] = {-120, -75, 0, 75, 120};
static const int32_t CAROUSEL_PY[] = {-25, -12, 0, -12, -25};
static const int32_t CAROUSEL_SC[] = {128, 184, 256, 184, 128};
static const int32_t CAROUSEL_OP[] = {LV_OPA_40, LV_OPA_70, LV_OPA_COVER, LV_OPA_70, LV_OPA_40};
static const int32_t CAROUSEL_Z[] = {0, 1, 2, 1, 0};
#define CAROUSEL_SLOTS  5
#define CAROUSEL_CENTER 2

typedef struct {
  const char *name;
  const char *icon_frames[MENU_ITEM_FRAME_COUNT];
  const char *base_frames[MENU_ITEM_FRAME_COUNT];
  lv_image_dsc_t *icon_dscs[MENU_ITEM_FRAME_COUNT];
  lv_image_dsc_t *base_dscs[MENU_ITEM_FRAME_COUNT];
  screen_id_t target;
} menu_ui_item_t;

#define BASE_FRAMES                   \
  {"/assets/frames/base_frame_0.bin", \
   "/assets/frames/base_frame_1.bin", \
   "/assets/frames/base_frame_2.bin"}

static menu_ui_item_t s_menu_data[] = {
    {"WIFI",
     {"/assets/frames/wifi_frame_0.bin",
      "/assets/frames/wifi_frame_1.bin",
      "/assets/frames/wifi_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_WIFI_MENU},
    {"BLUETOOTH",
     {"/assets/frames/ble_frame_0.bin",
      "/assets/frames/ble_frame_1.bin",
      "/assets/frames/ble_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_BLE_MENU},
    {"NFC",
     {"/assets/frames/nfc_frame_0.bin",
      "/assets/frames/nfc_frame_1.bin",
      "/assets/frames/nfc_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_NFC_MENU},
    {"RFID",
     {"/assets/frames/rfid_frame_0.bin",
      "/assets/frames/rfid_frame_1.bin",
      "/assets/frames/rfid_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_RFID_MENU},
    {"INFRARED",
     {"/assets/frames/ir_frame_0.bin",
      "/assets/frames/ir_frame_1.bin",
      "/assets/frames/ir_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_IR_MENU},
    {"SUB-GHZ",
     {"/assets/frames/subghz_frame_0.bin",
      "/assets/frames/subghz_frame_1.bin",
      "/assets/frames/subghz_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_SUBGHZ_MENU},
    {"LORA",
     {"/assets/frames/lora_frame_0.bin",
      "/assets/frames/lora_frame_1.bin",
      "/assets/frames/lora_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_LORA_CHAT},
    {"BADUSB",
     {"/assets/frames/usb_frame_0.bin",
      "/assets/frames/usb_frame_1.bin",
      "/assets/frames/usb_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_BADUSB_MENU},
    {"GPIO",
     {"/assets/frames/gpios_frame_0.bin",
      "/assets/frames/gpios_frame_1.bin",
      "/assets/frames/gpios_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_GPIO},
    {"CONFIGURATION",
     {"/assets/frames/config_frame_0.bin",
      "/assets/frames/config_frame_1.bin",
      "/assets/frames/config_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_SETTINGS},
    {"FILES",
     {"/assets/frames/file_frame_0.bin",
      "/assets/frames/file_frame_1.bin",
      "/assets/frames/file_frame_2.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_FILES},
    {"PLAYER",
     {"/assets/frames/player_glyph.bin",
      "/assets/frames/player_glyph.bin",
      "/assets/frames/player_glyph.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_PLAYER},
    {"DEV",
     {"/assets/frames/dev_glyph.bin",
      "/assets/frames/dev_glyph.bin",
      "/assets/frames/dev_glyph.bin"},
     BASE_FRAMES,
     {NULL},
     {NULL},
     SCREEN_DEV_MENU},
};

extern lv_group_t *main_group;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_label = NULL;
static lv_obj_t *s_base_imgs[MENU_ITEM_COUNT];
static lv_obj_t *s_icon_imgs[MENU_ITEM_COUNT];
static page_dots_t s_page_dots;
static uint8_t s_selected = 0;
static lv_font_t *s_font = NULL;
static bool s_is_animating = false;

static int32_t carousel_slot(size_t item_idx);
static void load_item_frame(size_t item_idx, int frame);
static void place_item(size_t item_idx, bool anim);
static void fix_z_order(void);
static void update_view(bool anim);
static void on_anim_done(lv_anim_t *a);
static void on_key_event(lv_event_t *e);

static int32_t carousel_slot(size_t item_idx) {
  int32_t n = (int32_t)MENU_ITEM_COUNT;
  int32_t d = ((int32_t)item_idx - (int32_t)s_selected + n) % n;
  if (d > n / 2)
    d -= n;
  int32_t slot = CAROUSEL_CENTER + d;
  return (slot >= 0 && slot < CAROUSEL_SLOTS) ? slot : -1;
}

static void on_anim_done(lv_anim_t *a) {
  (void)a;
  s_is_animating = false;
}

static void load_item_frame(size_t item_idx, int frame) {
  if (s_menu_data[item_idx].icon_frames[frame] != NULL &&
      s_menu_data[item_idx].icon_dscs[frame] == NULL)
    s_menu_data[item_idx].icon_dscs[frame] = assets_get(s_menu_data[item_idx].icon_frames[frame]);
  if (s_menu_data[item_idx].base_frames[frame] != NULL &&
      s_menu_data[item_idx].base_dscs[frame] == NULL)
    s_menu_data[item_idx].base_dscs[frame] = assets_get(s_menu_data[item_idx].base_frames[frame]);
}

static void place_item(size_t item_idx, bool anim) {
  int32_t slot = carousel_slot(item_idx);

  if (slot < 0) {
    lv_obj_set_style_opa(s_base_imgs[item_idx], LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(s_icon_imgs[item_idx], LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_base_imgs[item_idx], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_icon_imgs[item_idx], LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_remove_flag(s_base_imgs[item_idx], LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s_icon_imgs[item_idx], LV_OBJ_FLAG_HIDDEN);

  int frame = (slot < CAROUSEL_CENTER) ? 1 : (slot > CAROUSEL_CENTER) ? 2 : 0;
  load_item_frame(item_idx, frame);

  if (s_menu_data[item_idx].base_dscs[frame] != NULL)
    lv_image_set_src(s_base_imgs[item_idx], s_menu_data[item_idx].base_dscs[frame]);
  if (s_menu_data[item_idx].icon_dscs[frame] != NULL)
    lv_image_set_src(s_icon_imgs[item_idx], s_menu_data[item_idx].icon_dscs[frame]);

  int32_t tx = CAROUSEL_PX[slot];
  int32_t ty = CAROUSEL_PY[slot];
  int32_t ts = CAROUSEL_SC[slot];
  int32_t to = CAROUSEL_OP[slot];
  bool is_center = (slot == CAROUSEL_CENTER);

  if (is_center) {
    lv_obj_set_style_opa(s_base_imgs[item_idx], LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_icon_imgs[item_idx], LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(s_base_imgs[item_idx], LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(s_icon_imgs[item_idx], LV_OPA_COVER, 0);
  }

  if (anim) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_duration(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

    lv_anim_set_var(&a, s_base_imgs[item_idx]);
    lv_anim_set_values(&a, lv_obj_get_x_aligned(s_base_imgs[item_idx]), tx);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    if (is_center)
      lv_anim_set_completed_cb(&a, on_anim_done);
    lv_anim_start(&a);

    lv_anim_set_var(&a, s_icon_imgs[item_idx]);
    lv_anim_set_values(&a, lv_obj_get_x_aligned(s_icon_imgs[item_idx]), tx);
    lv_anim_start(&a);

    lv_anim_set_var(&a, s_base_imgs[item_idx]);
    lv_anim_set_values(&a, lv_obj_get_y_aligned(s_base_imgs[item_idx]), ty);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_start(&a);

    lv_anim_set_var(&a, s_icon_imgs[item_idx]);
    lv_anim_set_values(&a, lv_obj_get_y_aligned(s_icon_imgs[item_idx]), ty);
    lv_anim_start(&a);

    lv_anim_set_var(&a, s_base_imgs[item_idx]);
    lv_anim_set_values(&a, lv_image_get_scale(s_base_imgs[item_idx]), ts);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_image_set_scale);
    lv_anim_start(&a);

    lv_anim_set_var(&a, s_icon_imgs[item_idx]);
    lv_anim_set_values(&a, lv_image_get_scale(s_icon_imgs[item_idx]), ts);
    lv_anim_start(&a);

    lv_anim_set_var(&a, s_base_imgs[item_idx]);
    lv_anim_set_values(&a, lv_obj_get_style_opa(s_base_imgs[item_idx], 0), to);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_start(&a);

    lv_anim_set_var(&a, s_icon_imgs[item_idx]);
    lv_anim_set_values(&a, lv_obj_get_style_opa(s_icon_imgs[item_idx], 0), to);
    lv_anim_start(&a);
  } else {
    lv_obj_align(s_base_imgs[item_idx], LV_ALIGN_CENTER, tx, ty);
    lv_obj_align(s_icon_imgs[item_idx], LV_ALIGN_CENTER, tx, ty);
    lv_image_set_scale(s_base_imgs[item_idx], ts);
    lv_image_set_scale(s_icon_imgs[item_idx], ts);
    lv_obj_set_style_opa(s_base_imgs[item_idx], to, 0);
    lv_obj_set_style_opa(s_icon_imgs[item_idx], to, 0);
  }
}

static void fix_z_order(void) {
  typedef struct {
    size_t item_idx;
    int32_t z;
  } z_entry_t;

  z_entry_t visible[CAROUSEL_SLOTS];
  size_t count = 0;

  for (size_t i = 0; i < MENU_ITEM_COUNT; i++) {
    int32_t slot = carousel_slot(i);
    if (slot >= 0 && count < CAROUSEL_SLOTS) {
      visible[count].item_idx = i;
      visible[count].z = CAROUSEL_Z[slot];
      count++;
    }
  }

  for (size_t i = 0; i < count - 1; i++) {
    for (size_t j = i + 1; j < count; j++) {
      if (visible[i].z > visible[j].z) {
        z_entry_t tmp = visible[i];
        visible[i] = visible[j];
        visible[j] = tmp;
      }
    }
  }

  for (size_t i = 0; i < count; i++) {
    lv_obj_move_foreground(s_base_imgs[visible[i].item_idx]);
    lv_obj_move_foreground(s_icon_imgs[visible[i].item_idx]);
  }
}

static void update_view(bool anim) {
  lv_label_set_text_fmt(
      s_label, LV_SYMBOL_LEFT "   %s   " LV_SYMBOL_RIGHT, s_menu_data[s_selected].name);

  if (anim) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_label);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_duration(&a, LABEL_FADE_MS);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_start(&a);
  }

  page_dots_set(&s_page_dots, s_selected);

  for (size_t i = 0; i < MENU_ITEM_COUNT; i++)
    place_item(i, anim);

  fix_z_order();
}

static void on_key_event(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;

  uint32_t k = lv_event_get_key(e);
  size_t n = MENU_ITEM_COUNT;

  if (k == LV_KEY_RIGHT || k == LV_KEY_LEFT) {
    if (s_is_animating)
      return;
    s_is_animating = true;

    if (k == LV_KEY_RIGHT)
      s_selected = (s_selected + 1) % n;
    else
      s_selected = (s_selected == 0) ? (uint8_t)(n - 1) : s_selected - 1;

    ui_feedback(UI_FB_NAV);
    update_view(true);
    return;
  }

  if (k == LV_KEY_ESC) {
    ui_switch_screen(SCREEN_HOME);
    return;
  }

  if (k == LV_KEY_ENTER) {
    ui_switch_screen(s_menu_data[s_selected].target);
  }
}

void ui_menu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_is_animating = false;

  for (size_t i = 0; i < MENU_ITEM_COUNT; i++) {
    for (int f = 0; f < MENU_ITEM_FRAME_COUNT; f++) {
      s_menu_data[i].icon_dscs[f] = NULL;
      s_menu_data[i].base_dscs[f] = NULL;
    }
  }

  s_screen = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  if (s_font == NULL)
    s_font = lv_binfont_create(FONT_PATH);

  for (size_t i = 0; i < MENU_ITEM_COUNT; i++) {
    s_base_imgs[i] = lv_image_create(s_screen);
    lv_obj_align(s_base_imgs[i], LV_ALIGN_CENTER, 0, ICON_CENTER_OFFSET_Y);
    lv_image_set_antialias(s_base_imgs[i], false);

    s_icon_imgs[i] = lv_image_create(s_screen);
    lv_obj_align(s_icon_imgs[i], LV_ALIGN_CENTER, 0, ICON_CENTER_OFFSET_Y);
    lv_image_set_antialias(s_icon_imgs[i], false);
  }

  header_ui_create(s_screen);

  s_label = lv_label_create(s_screen);
  lv_obj_align(s_label, LV_ALIGN_BOTTOM_MID, 0, LABEL_OFFSET_Y);

  lv_obj_set_style_text_color(s_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);

  s_page_dots = page_dots_create(s_screen, MENU_ITEM_COUNT, LV_ALIGN_BOTTOM_MID, 0, DOTS_OFFSET_Y);

  update_view(false);

  lv_obj_add_event_cb(s_screen, on_key_event, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_add_obj(main_group, s_screen);
    lv_group_focus_obj(s_screen);
  }

  ui_screen_load_owned(&s_screen, s_screen);
}
