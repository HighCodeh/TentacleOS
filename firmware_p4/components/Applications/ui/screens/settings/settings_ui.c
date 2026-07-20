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

#include "settings_ui.h"

#include "esp_log.h"

#include "ui_theme.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "buttons_gpio.h"
#include "c5_flasher.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SETTINGS_UI";

#define NAV_TIMER_PERIOD_MS 50
#define ENTRY_FADE_MS       180

#define C5_PROGRESS_TICK_MS       200
#define C5_DISMISS_DELAY_MS       2000
#define C5_FLASH_TASK_STACK       4096
#define C5_FLASH_TASK_PRIO        5
#define C5_ROM_FLASH_TASK_STACK   8192
#define C5_ROM_FLASH_TASK_PRIO    5
#define C5_PASSTHROUGH_TASK_STACK 4096
#define C5_PASSTHROUGH_TASK_PRIO  6
#define REBOOT_DELAY_MS           80

#define ACTION_TOGGLE_ROTATION (-1)
#define ACTION_FLASH_C5        (-2)
#define ACTION_C5_PASSTHROUGH  (-3)
#define ACTION_REBOOT_P4       (-4)
#define ACTION_FLASH_C5_ROM    (-5)
#define ACTION_RELEASE_C5_UART (-6)

#define GOTO_LAB (-20)
#define GOTO_DEV (-21)

typedef enum {
  VIEW_MAIN = 0,
  VIEW_LAB,
  VIEW_DEV,
} settings_view_t;

typedef struct {
  const char *name;
  const char *icon;
  int target;
} settings_item_t;

static const settings_item_t MAIN_ITEMS[] = {
    {"CONNECTION", "/assets/icons/wifi_menu_icon.bin", SCREEN_CONNECTION_SETTINGS},
    {"DISPLAY", "/assets/icons/display_menu_icon.bin", SCREEN_DISPLAY_SETTINGS},
    {"INTERFACE", "/assets/icons/interface_menu_icon.bin", SCREEN_INTERFACE_SETTINGS},
    {"THEME", "/assets/icons/theme_menu_icon.bin", SCREEN_THEME_SELECTOR},
    {"ROTATE SCREEN", "/assets/icons/rotate_menu_icon.bin", ACTION_TOGGLE_ROTATION},
    {"SOUND", "/assets/icons/volume_icon.bin", SCREEN_SOUND_SETTINGS},
    {"AUDIO & HAPTICS", "/assets/icons/volume_icon.bin", GOTO_LAB},
    {"BATTERY", "/assets/icons/battery_menu_icon.bin", SCREEN_BATTERY_SETTINGS},
    {"POWER", "/assets/icons/power_icon.bin", SCREEN_POWER},
    {"DEVELOPER", "/assets/icons/push_icon.bin", GOTO_DEV},
    {"ABOUT", "/assets/icons/about_menu_icon.bin", SCREEN_ABOUT_SETTINGS},
};
#define MAIN_COUNT ((int)(sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0])))

typedef struct {
  int before;
  const char *title;
} settings_section_t;
static const settings_section_t MAIN_SECTIONS[] = {
    {0, "Connectivity"},
    {1, "Interface"},
    {5, "Sound & Haptics"},
    {7, "Power"},
    {9, "System"},
};
#define MAIN_SECTION_COUNT ((int)(sizeof(MAIN_SECTIONS) / sizeof(MAIN_SECTIONS[0])))

static const settings_item_t LAB_ITEMS[] = {
    {"VIBRATION", "/assets/icons/phone_icon.bin", SCREEN_HAPTIC},
    {"SPEAKER", "/assets/icons/volume_icon.bin", SCREEN_SPEAKER},
    {"MIC -> SPEAKER", "/assets/icons/volume_icon.bin", SCREEN_MIC_REC},
    {"SPECTRUM", "/assets/icons/radar_icon.bin", SCREEN_SPECTRUM},
};
#define LAB_COUNT ((int)(sizeof(LAB_ITEMS) / sizeof(LAB_ITEMS[0])))

static const settings_item_t DEV_ITEMS[] = {
    {"UPDATE C5", "/assets/icons/recharge_menu_icon.bin", ACTION_FLASH_C5},
    {"FLASH C5 (ROM)", "/assets/icons/push_icon.bin", ACTION_FLASH_C5_ROM},
    {"RELEASE C5 UART", "/assets/icons/push_icon.bin", ACTION_RELEASE_C5_UART},
    {"C5 PASSTHROUGH", "/assets/icons/push_icon.bin", ACTION_C5_PASSTHROUGH},
    {"RESTART P4", "/assets/icons/recharge_menu_icon.bin", ACTION_REBOOT_P4},
};
#define DEV_COUNT ((int)(sizeof(DEV_ITEMS) / sizeof(DEV_ITEMS[0])))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static settings_view_t s_view = VIEW_MAIN;

static lv_obj_t *s_confirm_overlay = NULL;

static lv_obj_t *s_c5_overlay = NULL;
static lv_obj_t *s_c5_status_label = NULL;
static lv_obj_t *s_c5_bar = NULL;
static lv_timer_t *s_c5_prog_timer = NULL;
static bool s_c5_in_progress = false;

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void build_settings_view(settings_view_t view);

static const settings_item_t *
view_table(settings_view_t view, int *count, const char **title, const char **icon) {
  switch (view) {
    case VIEW_LAB:
      if (count)
        *count = LAB_COUNT;
      if (title)
        *title = "AUDIO & HAPTICS";
      if (icon)
        *icon = "/assets/icons/volume_icon.bin";
      return LAB_ITEMS;
    case VIEW_DEV:
      if (count)
        *count = DEV_COUNT;
      if (title)
        *title = "DEVELOPER";
      if (icon)
        *icon = "/assets/icons/push_icon.bin";
      return DEV_ITEMS;
    case VIEW_MAIN:
    default:
      if (count)
        *count = MAIN_COUNT;
      if (title)
        *title = "SETTINGS";
      if (icon)
        *icon = "/assets/icons/config_icon.bin";
      return MAIN_ITEMS;
  }
}

static void show_rotation_confirm(void) {
  if (s_confirm_overlay != NULL)
    return;

  s_confirm_overlay = lv_obj_create(s_screen);
  lv_obj_set_size(s_confirm_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_center(s_confirm_overlay);
  lv_obj_remove_flag(s_confirm_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_confirm_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_confirm_overlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(s_confirm_overlay, 0, 0);
  lv_obj_set_style_pad_all(s_confirm_overlay, 0, 0);

  lv_obj_t *box = lv_obj_create(s_confirm_overlay);
  lv_obj_set_size(box, 200, 120);
  lv_obj_center(box);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(box, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 10, 0);
  lv_obj_set_style_pad_all(box, 8, 0);

  lv_obj_t *title = lv_label_create(box);
  lv_label_set_text(title, "[ ROTATE? ]");
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *body = lv_label_create(box);
  lv_label_set_text(body, "Switch portrait/\nlandscape now?");
  lv_obj_set_style_text_color(body, current_theme.text_main, 0);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(body, LV_ALIGN_CENTER, 0, 4);

  lv_obj_t *hint = lv_label_create(box);
  lv_label_set_text(hint, "OK = YES   BACK = NO");
  lv_obj_set_style_text_color(hint, current_theme.border_accent, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void hide_rotation_confirm(void) {
  if (s_confirm_overlay) {
    lv_obj_del(s_confirm_overlay);
    s_confirm_overlay = NULL;
  }
}

static void show_c5_progress(const char *msg) {
  if (s_c5_overlay == NULL) {
    s_c5_overlay = lv_obj_create(s_screen);
    lv_obj_set_size(s_c5_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_c5_overlay);
    lv_obj_remove_flag(s_c5_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_c5_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_c5_overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_c5_overlay, 0, 0);

    lv_obj_t *box = lv_obj_create(s_c5_overlay);
    lv_obj_set_size(box, 200, 130);
    lv_obj_center(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, current_theme.border_accent, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 10, 0);

    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, "[ UPDATING C5 ]");
    lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_c5_status_label = lv_label_create(box);
    lv_label_set_text(s_c5_status_label, msg ? msg : "Starting...");
    lv_obj_set_style_text_color(s_c5_status_label, current_theme.text_main, 0);
    lv_obj_set_style_text_align(s_c5_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_c5_status_label, LV_ALIGN_CENTER, 0, -4);
    lv_label_set_long_mode(s_c5_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_c5_status_label, 180);

    s_c5_bar = lv_bar_create(box);
    lv_obj_set_size(s_c5_bar, 170, 12);
    lv_obj_align(s_c5_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(s_c5_bar, 0, 100);
    lv_bar_set_value(s_c5_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_c5_bar, lv_color_hex(0x202028), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_c5_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_c5_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_c5_bar, lv_color_hex(0x00E676), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_c5_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_c5_bar, 4, LV_PART_INDICATOR);
    lv_obj_add_flag(s_c5_bar, LV_OBJ_FLAG_HIDDEN);
  } else if (s_c5_status_label && msg) {
    lv_label_set_text(s_c5_status_label, msg);
  }
}

static void hide_c5_progress(void) {
  if (s_c5_prog_timer) {
    lv_timer_delete(s_c5_prog_timer);
    s_c5_prog_timer = NULL;
  }
  if (s_c5_overlay) {
    lv_obj_del(s_c5_overlay);
    s_c5_overlay = NULL;
    s_c5_status_label = NULL;
    s_c5_bar = NULL;
  }
}

static void c5_flash_done_on_lvgl(void *data) {
  esp_err_t r = (esp_err_t)(intptr_t)data;
  if (r == ESP_OK) {
    show_c5_progress("DONE — C5 rebooted\ninto new firmware.");
    if (s_c5_bar)
      lv_bar_set_value(s_c5_bar, 100, LV_ANIM_OFF);
  } else {
    show_c5_progress("FAILED. Check serial\nlog for details.");
  }

  if (s_c5_prog_timer) {
    lv_timer_delete(s_c5_prog_timer);
    s_c5_prog_timer = NULL;
  }
  s_c5_in_progress = false;

  static lv_timer_t *dismiss_t = NULL;
  if (dismiss_t == NULL) {
    dismiss_t = lv_timer_create((lv_timer_cb_t)hide_c5_progress, C5_DISMISS_DELAY_MS, NULL);
    lv_timer_set_repeat_count(dismiss_t, 1);
  }
}

static void c5_flash_task(void *arg) {
  (void)arg;
  esp_err_t init_r = c5_flasher_init();
  esp_err_t r = (init_r != ESP_OK) ? init_r : c5_flasher_update(NULL, 0);
  ESP_LOGI(TAG, "c5_flasher result: %s", esp_err_to_name(r));

  lv_async_call(c5_flash_done_on_lvgl, (void *)(intptr_t)r);
  vTaskDelete(NULL);
}

static void c5_progress_tick(lv_timer_t *t) {
  (void)t;
  if (s_c5_bar == NULL)
    return;
  uint32_t sent = 0, total = 0;
  c5_flasher_progress(&sent, &total);
  int pct = total ? (int)((uint64_t)sent * 100 / total) : 0;
  lv_bar_set_value(s_c5_bar, pct, LV_ANIM_OFF);
}

static void start_c5_flash(void) {
  if (s_c5_in_progress)
    return;
  s_c5_in_progress = true;
  show_c5_progress("Updating C5 (OTA)...\nDo not power off.");

  if (s_c5_bar)
    lv_obj_remove_flag(s_c5_bar, LV_OBJ_FLAG_HIDDEN);
  if (s_c5_prog_timer == NULL)
    s_c5_prog_timer = lv_timer_create(c5_progress_tick, C5_PROGRESS_TICK_MS, NULL);

  xTaskCreate(c5_flash_task, "c5_flash", C5_FLASH_TASK_STACK, NULL, C5_FLASH_TASK_PRIO, NULL);
}

static void c5_rom_flash_task(void *arg) {
  (void)arg;
  esp_err_t r = c5_flasher_rom_flash();
  ESP_LOGI(TAG, "c5_rom_flash result: %s", esp_err_to_name(r));
  lv_async_call(c5_flash_done_on_lvgl, (void *)(intptr_t)r);
  vTaskDelete(NULL);
}

static void start_c5_rom_flash(void) {
  if (s_c5_in_progress)
    return;
  s_c5_in_progress = true;
  show_c5_progress(
      "ROM flash (blank C5).\nC5 must be in download\nmode. ~2-3 min. Don't\npower off.");
  xTaskCreate(c5_rom_flash_task,
              "c5_rom_flash",
              C5_ROM_FLASH_TASK_STACK,
              NULL,
              C5_ROM_FLASH_TASK_PRIO,
              NULL);
}

static void c5_passthrough_task(void *arg) {
  (void)arg;
  c5_passthrough_run();
  vTaskDelete(NULL);
}

static void start_c5_passthrough(void) {
  show_c5_progress("Passthrough ACTIVE.\n\n"
                   "Strap C5 GPIO28 -> GND\n"
                   "Power-cycle. Then on PC:\n"
                   "esptool --chip esp32c5 -p\n"
                   "/dev/cu.usbmodem<N> flash\n\n"
                   "BACK = reboot P4.");
  xTaskCreate(c5_passthrough_task,
              "c5_passthru",
              C5_PASSTHROUGH_TASK_STACK,
              NULL,
              C5_PASSTHROUGH_TASK_PRIO,
              NULL);
}

static bool run_action(int target) {
  switch (target) {
    case ACTION_TOGGLE_ROTATION:

      show_rotation_confirm();
      return true;
    case ACTION_FLASH_C5:

      start_c5_flash();
      return true;
    case ACTION_FLASH_C5_ROM:

      start_c5_rom_flash();
      return true;
    case ACTION_RELEASE_C5_UART:

      c5_flasher_release_uart();
      show_c5_progress(
          "C5 UART released.\nGPIO38/39 hi-Z.\nUse external serial.\nReboot P4 to restore.");
      return true;
    case ACTION_C5_PASSTHROUGH:

      start_c5_passthrough();
      return true;
    case ACTION_REBOOT_P4:

      ESP_LOGW(TAG, "User-requested P4 reboot from Settings.");
      vTaskDelay(pdMS_TO_TICKS(REBOOT_DELAY_MS));
      esp_restart();
      return true;
    default:
      return false;
  }
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (s_confirm_overlay != NULL) {
    if (ok && !s_btn_ok_last) {
      hide_rotation_confirm();
      ui_manager_relayout_current();
    } else if (back && !s_btn_back_last) {
      hide_rotation_confirm();
    }
    s_btn_up_last = up;
    s_btn_down_last = down;
    s_btn_left_last = left;
    s_btn_right_last = right;
    s_btn_ok_last = ok;
    s_btn_back_last = back;
    return;
  }

  if (down && !s_btn_down_last)
    menu_component_next(&s_menu);

  if (up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if ((back && !s_btn_back_last) || (left && !s_btn_left_last)) {
    if (s_view != VIEW_MAIN) {
      build_settings_view(VIEW_MAIN);
    } else {
      ui_switch_screen(SCREEN_MENU);
    }

    s_btn_up_last = up;
    s_btn_down_last = down;
    s_btn_left_last = left;
    s_btn_right_last = right;
    s_btn_ok_last = ok;
    s_btn_back_last = back;
    return;
  }

  if ((ok && !s_btn_ok_last) || (right && !s_btn_right_last)) {
    int count = 0;
    const settings_item_t *items = view_table(s_view, &count, NULL, NULL);
    int sel = menu_component_get_selected(&s_menu);
    if (sel >= 0 && sel < count) {
      int target = items[sel].target;
      if (target == GOTO_LAB) {
        build_settings_view(VIEW_LAB);
      } else if (target == GOTO_DEV) {
        build_settings_view(VIEW_DEV);
      } else if (!run_action(target)) {
        ui_switch_screen(target);
      }

      s_btn_up_last = up;
      s_btn_down_last = down;
      s_btn_left_last = left;
      s_btn_right_last = right;
      s_btn_ok_last = ok;
      s_btn_back_last = back;
      return;
    }
  }

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

static void build_settings_view(settings_view_t view) {
  lv_obj_t *prev = s_screen;
  s_view = view;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  int count = 0;
  const char *title = NULL;
  const char *icon = NULL;
  const settings_item_t *items = view_table(view, &count, &title, &icon);

  s_menu = menu_component_create(s_screen, title, icon);
  for (int i = 0; i < count; i++) {
    if (view == VIEW_MAIN) {
      for (int s = 0; s < MAIN_SECTION_COUNT; s++)
        if (MAIN_SECTIONS[s].before == i)
          menu_component_add_section(&s_menu, MAIN_SECTIONS[s].title);
    }
    menu_component_add_item(&s_menu, items[i].icon, items[i].name);
  }

  if (s_menu.items_cont != NULL)
    lv_obj_fade_in(s_menu.items_cont, ENTRY_FADE_MS, 0);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_PERIOD_MS, NULL);

  ui_screen_load(s_screen);
  if (prev != NULL)
    lv_obj_del(prev);
}

void ui_settings_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_confirm_overlay = NULL;
  build_settings_view(VIEW_MAIN);
}
