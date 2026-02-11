#include "menu_ui.h"
#include "home_ui.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_theme.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <sys/stat.h>
#include "settings_ui.h"
#include "buzzer.h"
#include "assets_manager.h"

extern lv_group_t * main_group;
static const char *TAG = "UI_MENU";

typedef struct {
  const char * name;
  const char * path_frames[3]; 
  uint16_t dims[3][2];         
  lv_image_dsc_t * dscs[3];    
} menu_item_t;

static menu_item_t menu_data[] = {
  {"BLUETOOTH",      {"/assets/frames/bluetooth_frame_0.bin", "/assets/frames/bluetooth_frame_1.bin", "/assets/frames/bluetooth_frame_2.bin"}, {{39,39}, {39,50}, {39,50}}, {NULL, NULL, NULL}},
  {"BAD USB",        {"/assets/frames/files_frame_0.bin", "/assets/frames/files_frame_1.bin", "/assets/frames/files_frame_2.bin"}, {{38,38}, {39,51}, {39,51}}, {NULL, NULL, NULL}},
  {"WIFI",           {"/assets/frames/wifi_frame_0.bin", "/assets/frames/wifi_frame_1.bin", "/assets/frames/wifi_frame_2.bin"}, {{38,38}, {39,49}, {39,49}}, {NULL, NULL, NULL}},
  {"INFRARED",       {"/assets/frames/ir_frame_0.bin", "/assets/frames/ir_frame_1.bin", "/assets/frames/ir_frame_2.bin"}, {{38,38}, {40,51}, {40,51}}, {NULL, NULL, NULL}},
  {"CONFIGURATION",  {"/assets/frames/config_frame_0.bin", "/assets/frames/config_frame_1.bin", "/assets/frames/config_frame_2.bin"}, {{39,38}, {40,52}, {40,52}}, {NULL, NULL, NULL}},
  {"NFC",            {"/assets/frames/nfc_frame_0.bin", "/assets/frames/nfc_frame_1.bin", "/assets/frames/nfc_frame_2.bin"}, {{38,38}, {41,50}, {41,50}}, {NULL, NULL, NULL}},
  {"RADIO FREQUENCY", {"/assets/frames/radio_frame_0.bin", "/assets/frames/radio_frame_1.bin", "/assets/frames/radio_frame_2.bin"}, {{38,38}, {41,50}, {41,50}}, {NULL, NULL, NULL}},
  {"BROWSE FILES",   {"/assets/frames/files_frame_0.bin", "/assets/frames/files_frame_1.bin", "/assets/frames/files_frame_2.bin"}, {{38,38}, {39,51}, {39,51}}, {NULL, NULL, NULL}},
};

#define MENU_ITEM_COUNT (sizeof(menu_data) / sizeof(menu_data[0]))

static lv_obj_t * menu_screen = NULL;
static lv_obj_t * menu_indicator_label = NULL;
static lv_obj_t * item_imgs[MENU_ITEM_COUNT];
static uint8_t menu_index = 0;
static lv_obj_t * menu_dots[MENU_ITEM_COUNT];

static const int32_t pos_x[] = {-130, -90, -55, 0, 55, 90, 130};
static const int32_t opas[]  = {LV_OPA_0, LV_OPA_30, LV_OPA_60, LV_OPA_COVER, LV_OPA_60, LV_OPA_30, LV_OPA_0};
static const int32_t scales[] = {100, 180, 260, 420, 260, 180, 100};

static void start_float_animation(lv_obj_t * obj) {
  lv_anim_del(obj, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_values(&a, 0, -8);
  lv_anim_set_duration(&a, 1500);
  lv_anim_set_playback_duration(&a, 1500);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_start(&a);
}

static int get_visual_position(int item_idx) {
  int diff = (item_idx - menu_index + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
  if (diff > MENU_ITEM_COUNT / 2) diff -= MENU_ITEM_COUNT;
  int visual_pos = 3 + diff;
  if (visual_pos < 0) visual_pos = 0;
  if (visual_pos > 6) visual_pos = 6;
  return visual_pos;
}

static void animate_item_to_position(int item_idx) {
  lv_obj_t * obj = item_imgs[item_idx];
  int pos_idx = get_visual_position(item_idx);
  int frame_type = (pos_idx < 3) ? 1 : (pos_idx > 3) ? 2 : 0;

  if(!menu_data[item_idx].dscs[frame_type]) {
    menu_data[item_idx].dscs[frame_type] = assets_get(menu_data[item_idx].path_frames[frame_type]);
  }

  if(menu_data[item_idx].dscs[frame_type]) {
    lv_image_set_src(obj, menu_data[item_idx].dscs[frame_type]);
  }

  if(pos_idx == 3) {
    lv_obj_move_foreground(obj);
    start_float_animation(obj);
  } else {
    lv_anim_del(obj, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_obj_set_y(obj, 0);
  }

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_duration(&a, 450);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_values(&a, lv_obj_get_x_aligned(obj), pos_x[pos_idx]);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
  lv_anim_start(&a);
  lv_anim_set_values(&a, lv_image_get_scale(obj), scales[pos_idx]);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_image_set_scale);
  lv_anim_start(&a);
  lv_anim_set_values(&a, lv_obj_get_style_opa(obj, 0), opas[pos_idx]);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
  lv_anim_start(&a);
}

static void menu_update_ui(void) {
  lv_anim_t at;
  lv_anim_init(&at);
  lv_anim_set_var(&at, menu_indicator_label);
  lv_anim_set_values(&at, LV_OPA_0, LV_OPA_COVER);
  lv_anim_set_duration(&at, 400);
  lv_anim_set_exec_cb(&at, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
  lv_anim_start(&at);
  
  lv_label_set_text_fmt(menu_indicator_label, LV_SYMBOL_LEFT "   %s   " LV_SYMBOL_RIGHT, menu_data[menu_index].name);
  lv_obj_set_style_text_color(menu_indicator_label, current_theme.text_main, 0);

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    bool is_selected = (i == menu_index);
    lv_obj_set_style_bg_color(menu_dots[i], is_selected ? current_theme.border_accent : current_theme.text_main, 0);
    lv_obj_set_style_bg_opa(menu_dots[i], is_selected ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_size(menu_dots[i], is_selected ? 8 : 4, is_selected ? 8 : 4);
  }

  for(int i = 0; i < MENU_ITEM_COUNT; i++) {
    animate_item_to_position(i);
  }
}

static void menu_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_RIGHT) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        menu_index = (menu_index + 1) % MENU_ITEM_COUNT;
        menu_update_ui();
    } else if (key == LV_KEY_LEFT) {
        buzzer_play_sound_file("buzzer_scroll_tick");
        menu_index = (menu_index == 0) ? MENU_ITEM_COUNT - 1 : menu_index - 1;
        menu_update_ui();
    } else if (key == LV_KEY_ESC) {
        buzzer_play_sound_file("buzzer_click");
        ui_switch_screen(SCREEN_HOME);
    } else if (key == LV_KEY_ENTER) {
        buzzer_play_sound_file("buzzer_hacker_confirm");
        switch(menu_index) {
            case 0: ui_switch_screen(SCREEN_BLE_MENU); break;
            case 1: ui_switch_screen(SCREEN_BADUSB_MENU); break;
            case 2: ui_switch_screen(SCREEN_WIFI_MENU); break;
            case 4: ui_switch_screen(SCREEN_SETTINGS); break;
            case 6: ui_switch_screen(SCREEN_SUBGHZ_SPECTRUM); break;
            default: ESP_LOGW(TAG, "NOT DEFINED"); break;
        }
    }
}

void ui_menu_open(void) {
  if (menu_screen) { lv_obj_del(menu_screen); menu_screen = NULL; }
  menu_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(menu_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(menu_screen, LV_OBJ_FLAG_SCROLLABLE);

  for(int i = 0; i < MENU_ITEM_COUNT; i++) {
    item_imgs[i] = lv_image_create(menu_screen);
    lv_obj_align(item_imgs[i], LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(item_imgs[i], LV_OBJ_FLAG_SCROLLABLE);
  }

  header_ui_create(menu_screen);
  footer_ui_create(menu_screen);

  lv_obj_t * dots_cont = lv_obj_create(menu_screen);
  lv_obj_set_size(dots_cont, LV_SIZE_CONTENT, 20);
  lv_obj_align(dots_cont, LV_ALIGN_TOP_MID, 0, 45);
  lv_obj_set_flex_flow(dots_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_bg_opa(dots_cont, 0, 0);
  lv_obj_set_style_border_width(dots_cont, 0, 0);
  lv_obj_set_style_pad_gap(dots_cont, 10, 0);
  lv_obj_set_style_pad_all(dots_cont, 0, 0);
  lv_obj_remove_flag(dots_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(dots_cont, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    menu_dots[i] = lv_obj_create(dots_cont);
    lv_obj_set_style_radius(menu_dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(menu_dots[i], 0, 0);
    lv_obj_set_size(menu_dots[i], 4, 4);
    lv_obj_remove_flag(menu_dots[i], LV_OBJ_FLAG_SCROLLABLE);
  }

  menu_indicator_label = lv_label_create(menu_screen);
  lv_obj_align(menu_indicator_label, LV_ALIGN_BOTTOM_MID, 0, -55);

  menu_update_ui();
  lv_obj_add_event_cb(menu_screen, menu_event_cb, LV_EVENT_KEY, NULL);

  if (main_group) {
    lv_group_add_obj(main_group, menu_screen);
    lv_group_focus_obj(menu_screen);
  }
  lv_screen_load(menu_screen);
}