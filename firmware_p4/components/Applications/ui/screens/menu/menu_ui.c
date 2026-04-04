#include "menu_ui.h"
#include "home_ui.h"
#include "header_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include "assets_manager.h"
#include "page_dots_ui.h"
#include "st7789.h"

extern lv_group_t * main_group;
static const char *TAG = "UI_MENU";

typedef struct {
  const char * name;
  const char * icon_frames[3];
  const char * base_frames[3];
  lv_image_dsc_t * icon_dscs[3];
  lv_image_dsc_t * base_dscs[3];
} menu_item_t;

static menu_item_t menu_data[] = {
  {"WIFI",          {"/assets/frames/wifi_frame_0.bin",   "/assets/frames/wifi_frame_1.bin",   "/assets/frames/wifi_frame_2.bin"},   {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"BLUETOOTH",     {"/assets/frames/ble_frame_0.bin",    "/assets/frames/ble_frame_1.bin",    "/assets/frames/ble_frame_2.bin"},    {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"NFC",           {"/assets/frames/nfc_frame_0.bin",    "/assets/frames/nfc_frame_1.bin",    "/assets/frames/nfc_frame_2.bin"},    {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"INFRARED",      {"/assets/frames/ir_frame_0.bin",     "/assets/frames/ir_frame_1.bin",     "/assets/frames/ir_frame_2.bin"},     {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"SUB-GHZ",       {"/assets/frames/subghz_frame_0.bin", "/assets/frames/subghz_frame_1.bin", "/assets/frames/subghz_frame_2.bin"}, {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"LORA",          {"/assets/frames/lora_frame_0.bin",   "/assets/frames/lora_frame_1.bin",   "/assets/frames/lora_frame_2.bin"},   {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"GPIO",          {"/assets/frames/gpios_frame_0.bin",  "/assets/frames/gpios_frame_1.bin",  "/assets/frames/gpios_frame_2.bin"},  {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"CONFIGURATION", {"/assets/frames/config_frame_0.bin", "/assets/frames/config_frame_1.bin", "/assets/frames/config_frame_2.bin"}, {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"FILES",         {"/assets/frames/file_frame_0.bin",   "/assets/frames/file_frame_1.bin",   "/assets/frames/file_frame_2.bin"},   {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
  {"APPS",          {"/assets/frames/apps_frame_0.bin",   "/assets/frames/apps_frame_1.bin",   "/assets/frames/apps_frame_2.bin"},   {"/assets/frames/base_frame_0.bin", "/assets/frames/base_frame_1.bin", "/assets/frames/base_frame_2.bin"}, {NULL}, {NULL}},
};

#define N (sizeof(menu_data) / sizeof(menu_data[0]))
#define ANIM_MS 400

static lv_obj_t * scr = NULL;
static lv_obj_t * lbl = NULL;
static lv_obj_t * bi[N];   /* base images */
static lv_obj_t * ii[N];   /* icon images */
static page_dots_t pg_dots;
static uint8_t sel = 0;
static lv_font_t * fnt = NULL;
static bool animating = false;

/*              far-L   left  center  right  far-R  */
static const int PX[] = {-120, -75,    0,     75,   120};
static const int PY[] = {-25,  -12,    0,    -12,   -25};
static const int SC[] = {128,  184,   280,   184,   128};
static const int OP[] = {LV_OPA_40, LV_OPA_70, LV_OPA_COVER, LV_OPA_70, LV_OPA_40};

static int vis(int i) {
  int d = (i - sel + N) % N;
  if (d > (int)N / 2) d -= N;
  int v = 2 + d;
  return (v >= 0 && v <= 4) ? v : -1;
}

static void anim_done_cb(lv_anim_t * a) {
  animating = false;
}

static void load(int i, int f) {
  if (!menu_data[i].icon_dscs[f]) menu_data[i].icon_dscs[f] = assets_get(menu_data[i].icon_frames[f]);
  if (!menu_data[i].base_dscs[f]) menu_data[i].base_dscs[f] = assets_get(menu_data[i].base_frames[f]);
}

static void place(int i, bool anim) {
  int vp = vis(i);
  if (vp < 0) {
    /* Fade out then hide */
    lv_obj_set_style_opa(bi[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_opa(ii[i], LV_OPA_TRANSP, 0);
    lv_obj_add_flag(bi[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ii[i], LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(bi[i], LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(ii[i], LV_OBJ_FLAG_HIDDEN);

  int f = (vp < 2) ? 1 : (vp > 2) ? 2 : 0;
  load(i, f);
  if (menu_data[i].base_dscs[f]) lv_image_set_src(bi[i], menu_data[i].base_dscs[f]);
  if (menu_data[i].icon_dscs[f]) lv_image_set_src(ii[i], menu_data[i].icon_dscs[f]);

  int tx = PX[vp], ty = PY[vp], ts = SC[vp], to = OP[vp];
  bool is_center = (vp == 2);

  /* Center item must have zero transparency */
  if (is_center) {
    lv_obj_set_style_opa(bi[i], LV_OPA_COVER, 0);
    lv_obj_set_style_opa(ii[i], LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(bi[i], LV_OPA_COVER, 0);
    lv_obj_set_style_image_opa(ii[i], LV_OPA_COVER, 0);
  }

  if (anim) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_duration(&a, ANIM_MS);

    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);

    /* base X */
    lv_anim_set_var(&a, bi[i]);
    lv_anim_set_values(&a, lv_obj_get_x_aligned(bi[i]), tx);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    if (is_center) lv_anim_set_completed_cb(&a, anim_done_cb);
    lv_anim_start(&a);
    /* icon X */
    lv_anim_set_var(&a, ii[i]);
    lv_anim_set_values(&a, lv_obj_get_x_aligned(ii[i]), tx);
    lv_anim_start(&a);

    /* base Y */
    lv_anim_set_var(&a, bi[i]);
    lv_anim_set_values(&a, lv_obj_get_y_aligned(bi[i]), ty);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_start(&a);
    /* icon Y */
    lv_anim_set_var(&a, ii[i]);
    lv_anim_set_values(&a, lv_obj_get_y_aligned(ii[i]), ty);
    lv_anim_start(&a);

    /* base scale */
    lv_anim_set_var(&a, bi[i]);
    lv_anim_set_values(&a, lv_image_get_scale(bi[i]), ts);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_image_set_scale);
    lv_anim_start(&a);
    /* icon scale */
    lv_anim_set_var(&a, ii[i]);
    lv_anim_set_values(&a, lv_image_get_scale(ii[i]), ts);
    lv_anim_start(&a);

    /* base opacity */
    lv_anim_set_var(&a, bi[i]);
    lv_anim_set_values(&a, lv_obj_get_style_opa(bi[i], 0), to);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_start(&a);
    /* icon opacity */
    lv_anim_set_var(&a, ii[i]);
    lv_anim_set_values(&a, lv_obj_get_style_opa(ii[i], 0), to);
    lv_anim_start(&a);
  } else {
    lv_obj_align(bi[i], LV_ALIGN_CENTER, tx, ty);
    lv_obj_align(ii[i], LV_ALIGN_CENTER, tx, ty);
    lv_image_set_scale(bi[i], ts);
    lv_image_set_scale(ii[i], ts);
    lv_obj_set_style_opa(bi[i], to, 0);
    lv_obj_set_style_opa(ii[i], to, 0);
  }
}

static void zfix(void) {
  static const int zp[] = {0, 1, 2, 1, 0};
  typedef struct { int i, z; } z_t;
  z_t v[5]; int c = 0;
  for (int i = 0; i < (int)N; i++) {
    int vp = vis(i);
    if (vp >= 0 && c < 5) { v[c].i = i; v[c].z = zp[vp]; c++; }
  }
  for (int i = 0; i < c - 1; i++)
    for (int j = i + 1; j < c; j++)
      if (v[i].z > v[j].z) { z_t t = v[i]; v[i] = v[j]; v[j] = t; }
  for (int i = 0; i < c; i++) {
    lv_obj_move_foreground(bi[v[i].i]);
    lv_obj_move_foreground(ii[v[i].i]);
  }
}

static void update(bool anim) {
  lv_label_set_text_fmt(lbl, LV_SYMBOL_LEFT "   %s   " LV_SYMBOL_RIGHT, menu_data[sel].name);
  if (anim) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_start(&a);
  }

  page_dots_set(&pg_dots, sel);

  for (int i = 0; i < (int)N; i++) place(i, anim);
  zfix();
}

static void ev(lv_event_t * e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY) return;
  uint32_t k = lv_event_get_key(e);
  if (k == LV_KEY_RIGHT || k == LV_KEY_LEFT) {
    if (animating) return;
    animating = true;

    if (k == LV_KEY_RIGHT)
      sel = (sel + 1) % N;
    else
      sel = (sel == 0) ? N - 1 : sel - 1;
    update(true);
  } else if (k == LV_KEY_ESC) {
    ui_switch_screen(SCREEN_HOME);
  } else if (k == LV_KEY_ENTER) {
    switch (sel) {
      case 0: ui_switch_screen(SCREEN_WIFI_MENU); break;
      case 1: ui_switch_screen(SCREEN_BLE_MENU); break;
      case 2: ui_switch_screen(SCREEN_NFC_MENU); break;
      case 3: ui_switch_screen(SCREEN_IR_MENU); break;
      case 7: ui_switch_screen(SCREEN_SETTINGS); break;
      case 8: ui_switch_screen(SCREEN_FILES); break;
      default: ESP_LOGW(TAG, "NOT DEFINED"); break;
    }
  }
}

void ui_menu_open(void) {
  if (scr) { lv_obj_del(scr); scr = NULL; }
  animating = false;

  /* Invalidate cached asset pointers — they may be stale after theme change */
  for (int i = 0; i < (int)N; i++) {
    for (int f = 0; f < 3; f++) {
      menu_data[i].icon_dscs[f] = NULL;
      menu_data[i].base_dscs[f] = NULL;
    }
  }
  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  if (!fnt) fnt = lv_binfont_create("A:assets/fonts/Inter.bin");

  for (int i = 0; i < (int)N; i++) {
    bi[i] = lv_image_create(scr);
    lv_obj_align(bi[i], LV_ALIGN_CENTER, 0, -10);

    ii[i] = lv_image_create(scr);
    lv_obj_align(ii[i], LV_ALIGN_CENTER, 0, -10);
  }

  header_ui_create(scr);

  lbl = lv_label_create(scr);
  lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(lbl, fnt ? fnt : &lv_font_montserrat_14, 0);

  pg_dots = page_dots_create(scr, N, LV_ALIGN_BOTTOM_MID, 0, -20);

  update(false);
  lv_obj_add_event_cb(scr, ev, LV_EVENT_KEY, NULL);

  if (main_group) {
    lv_group_add_obj(main_group, scr);
    lv_group_focus_obj(scr);
  }
  lv_screen_load(scr);
}
