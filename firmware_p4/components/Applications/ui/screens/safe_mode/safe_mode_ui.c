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

#include "safe_mode_ui.h"

#include <stdint.h>

#include "lvgl.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "menu_component_ui.h"
#include "sys_prio.h"
#include "tos_factory_reset.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "SAFE_MODE_UI";

#define TITLE       "SAFE MODE"
#define TITLE_ICON  "/assets/icons/troubleshoot.bin"
#define MENU_HINT   "UP/DOWN  OK select  BACK"
#define CONFIRM_HINT "OK confirm   BACK cancel"

#define ROW_RESET_CONFIG 0
#define ROW_RESET_ALL    1
#define ROW_REBOOT       2

#define FACTORY_TASK_STACK 4096
#define REBOOT_DELAY_MS    1200

typedef enum {
  SM_MENU,
  SM_CONFIRM_CONFIG,
  SM_CONFIRM_ALL,
  SM_WORKING,
} sm_state_t;

typedef enum {
  ACTION_RESET_CONFIG,
  ACTION_RESET_ALL,
} sm_action_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static sm_state_t s_state = SM_MENU;

static void safe_mode_input(const input_event_t *ev, void *ctx);

static void factory_task(void *pv) {
  sm_action_t action = (sm_action_t)(intptr_t)pv;
  if (action == ACTION_RESET_ALL) {
    tos_factory_reset_all();
  } else {
    tos_factory_reset_config();
  }
  vTaskDelay(pdMS_TO_TICKS(REBOOT_DELAY_MS));
  esp_restart();
}

static lv_obj_t *make_screen_base(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  return scr;
}

static void build_menu(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_state = SM_MENU;
  s_screen = make_screen_base();

  s_menu = menu_component_create(s_screen, TITLE, TITLE_ICON);
  menu_component_add_item(&s_menu, "/assets/icons/settings.bin", "Reset config");
  menu_component_add_item(&s_menu, "/assets/icons/warning.bin", "Reset all");
  menu_component_add_item(&s_menu, "/assets/icons/restart_alt.bin", "Reboot");
  menu_component_set_hint(&s_menu, MENU_HINT);

  ui_input_set_screen_handler(safe_mode_input, NULL);
  ui_screen_load(s_screen);
}

static void build_message(const char *title, const char *body, const char *hint,
                          lv_color_t accent) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = make_screen_base();

  lv_obj_t *title_lbl = lv_label_create(s_screen);
  lv_label_set_text(title_lbl, title);
  lv_obj_set_style_text_color(title_lbl, accent, 0);
  lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *body_lbl = lv_label_create(s_screen);
  lv_label_set_long_mode(body_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body_lbl, LV_PCT(80));
  lv_label_set_text(body_lbl, body);
  lv_obj_set_style_text_color(body_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_align(body_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(body_lbl, LV_ALIGN_CENTER, 0, 0);

  if (hint != NULL) {
    lv_obj_t *hint_lbl = lv_label_create(s_screen);
    lv_label_set_text(hint_lbl, hint);
    lv_obj_set_style_text_color(hint_lbl, current_theme.border_inactive, 0);
    lv_obj_align(hint_lbl, LV_ALIGN_BOTTOM_MID, 0, -24);
  }

  ui_input_set_screen_handler(safe_mode_input, NULL);
  ui_screen_load(s_screen);
}

static void start_reset(sm_action_t action) {
  s_state = SM_WORKING;
  build_message("ERASING",
                action == ACTION_RESET_ALL ? "Erasing all data.\nThe device will reboot."
                                           : "Resetting configuration.\nThe device will reboot.",
                NULL, ui_theme_get_accent());
  // Run the wipe off the LVGL task so file deletion never stalls the renderer.
  xTaskCreatePinnedToCore(factory_task,
                          "factory_reset",
                          FACTORY_TASK_STACK,
                          (void *)(intptr_t)action,
                          SYS_PRIO_SERVICE_HI,
                          NULL,
                          SYS_CORE_UI);
}

static void safe_mode_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_state) {
    case SM_MENU:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav)
            menu_component_next(&s_menu);
          break;
        case INPUT_BTN_UP:
          if (nav)
            menu_component_prev(&s_menu);
          break;
        case INPUT_BTN_OK:
          if (press) {
            int sel = menu_component_get_selected(&s_menu);
            ui_feedback(UI_FB_SELECT);
            if (sel == ROW_RESET_CONFIG) {
              s_state = SM_CONFIRM_CONFIG;
              build_message("RESET CONFIG",
                            "Restore all settings to factory defaults?\nUser data is kept.",
                            CONFIRM_HINT, ui_theme_get_accent());
            } else if (sel == ROW_RESET_ALL) {
              s_state = SM_CONFIRM_ALL;
              build_message("RESET ALL",
                            "Erase config, all captures/loot and NVS?\nThis cannot be undone.",
                            CONFIRM_HINT, lv_palette_main(LV_PALETTE_RED));
            } else if (sel == ROW_REBOOT) {
              esp_restart();
            }
          }
          break;
        default:
          break;
      }
      break;

    case SM_CONFIRM_CONFIG:
      if (press && ev->button == INPUT_BTN_OK) {
        start_reset(ACTION_RESET_CONFIG);
      } else if (press && ev->button == INPUT_BTN_BACK) {
        build_menu();
      }
      break;

    case SM_CONFIRM_ALL:
      if (press && ev->button == INPUT_BTN_OK) {
        start_reset(ACTION_RESET_ALL);
      } else if (press && ev->button == INPUT_BTN_BACK) {
        build_menu();
      }
      break;

    case SM_WORKING:
      break;  // ignore input while the wipe runs
  }
}

void ui_safe_mode_open(void) {
  ESP_LOGW(TAG, "Entering safe mode UI");
  build_menu();
}
