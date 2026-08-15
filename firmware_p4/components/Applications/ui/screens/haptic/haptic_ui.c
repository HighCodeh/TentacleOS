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

#include "haptic_ui.h"

#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "drv2605l.h"
#include "menu_component_ui.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "HAPTIC_UI";

#define LIVE_ROW     0
#define CAT_ROW      1
#define FX_ROW       2
#define PAT_ROW      3
#define CAL_ROW      4

#define HAPTIC_TASK_STACK_SIZE 2816
#define HAPTIC_TASK_PRIORITY SYS_PRIO_SERVICE_HI

typedef struct {
  uint8_t id;
  const char *name;
} fx_t;

typedef struct {
  const char *name;
  const fx_t *fx;
  int count;
} fx_cat_t;

static const fx_t CLICKS[] = {
    {1, "Strong Click"},
    {2, "Str Click 60"},
    {4, "Sharp Click"},
    {5, "Shp Click 60"},
    {7, "Soft Bump"},
    {10, "Double Click"},
    {12, "Triple Click"},
    {13, "Soft Fuzz"},
};
static const fx_t TICKS[] = {
    {24, "Sharp Tick 1"},
    {25, "Sharp Tick 2"},
    {26, "Sharp Tick 3"},
    {23, "Med Click 3"},
    {17, "Med Click 1"},
};
static const fx_t BUZZES[] = {
    {14, "Strong Buzz"},
    {47, "Buzz 1"},
    {48, "Buzz 2"},
    {49, "Buzz 3"},
    {50, "Buzz 4"},
    {51, "Buzz 5"},
    {15, "Alert 750ms"},
    {16, "Alert 1s"},
};
static const fx_t PULSES[] = {
    {52, "Puls Strong1"},
    {53, "Puls Strong2"},
    {54, "Puls Med 1"},
    {55, "Puls Med 2"},
    {56, "Puls Sharp 1"},
    {57, "Puls Sharp 2"},
};
static const fx_t TRANS[] = {
    {58, "Tran Click 1"},
    {64, "Tran Hum 1"},
    {82, "Ramp Up Long"},
    {88, "Ramp Up Shrt"},
    {93, "Ramp Dn Long"},
    {99, "Ramp Dn Shrt"},
};
static const fx_t HUMS[] = {
    {119, "Smooth Hum 1"},
    {120, "Smooth Hum 2"},
    {121, "Smooth Hum 3"},
    {122, "Smooth Hum 4"},
    {123, "Smooth Hum 5"},
};

#define CAT(n, arr) {n, arr, (int)(sizeof(arr) / sizeof((arr)[0]))}
static const fx_cat_t CATS[] = {
    CAT("Clicks", CLICKS),
    CAT("Ticks", TICKS),
    CAT("Buzzes", BUZZES),
    CAT("Pulses", PULSES),
    CAT("Transitions", TRANS),
    CAT("Hums", HUMS),
};
#define NCAT ((int)(sizeof(CATS) / sizeof(CATS[0])))

enum { PAT_HEARTBEAT, PAT_SOS, PAT_NOTIFY, PAT_RAMP, PAT_THROB, PAT_COUNT };
static const char *const PAT_NAMES[PAT_COUNT] = {"Heartbeat", "SOS", "Notify", "Ramp", "Throb"};

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static volatile bool s_busy = false;
static volatile bool s_pat_cancel = false;
static bool s_rtp_live = false;
static int s_cat = 0, s_fx = 0, s_pat = 0;

static uint8_t rtp_for_level(int level) {
  if (level <= 0)
    return 0;
  if (level >= INTENSITY_BAR_STEPS)
    return 127;
  return (uint8_t)((level * 127) / INTENSITY_BAR_STEPS);
}

static void apply_live_intensity(void) {
  int lv = menu_component_get_intensity(&s_menu, LIVE_ROW);
  uint8_t rtp = rtp_for_level(lv);
  if (rtp == 0) {
    drv2605l_stop();
    s_rtp_live = false;
  } else {
    drv2605l_set_rtp(rtp);
    s_rtp_live = true;
  }
}

static void stop_live_intensity(void) {
  if (s_rtp_live) {
    drv2605l_stop();
    s_rtp_live = false;
  }
}

static void update_fx_label(void) {
  menu_component_set_selector_value(&s_menu, FX_ROW, CATS[s_cat].fx[s_fx].name);
}

static void play_current_fx(void) {
  drv2605l_play_effect(CATS[s_cat].fx[s_fx].id);
}

static void pat_dot(int rtp, int on_ms) {
  if (s_pat_cancel)
    return;
  drv2605l_set_rtp((uint8_t)rtp);
  vTaskDelay(pdMS_TO_TICKS(on_ms));
  drv2605l_stop();
  vTaskDelay(pdMS_TO_TICKS(110));
}

static void pattern_task(void *arg) {
  (void)arg;
  switch (s_pat) {
    case PAT_HEARTBEAT:
      for (int k = 0; k < 3 && !s_pat_cancel; k++) {
        drv2605l_play_effect(1);
        vTaskDelay(pdMS_TO_TICKS(130));
        drv2605l_play_effect(1);
        vTaskDelay(pdMS_TO_TICKS(560));
      }
      break;
    case PAT_SOS:
      for (int i = 0; i < 3 && !s_pat_cancel; i++)
        pat_dot(110, 120);
      vTaskDelay(pdMS_TO_TICKS(120));
      for (int i = 0; i < 3 && !s_pat_cancel; i++)
        pat_dot(110, 340);
      vTaskDelay(pdMS_TO_TICKS(120));
      for (int i = 0; i < 3 && !s_pat_cancel; i++)
        pat_dot(110, 120);
      break;
    case PAT_NOTIFY:
      drv2605l_play_effect(10);
      vTaskDelay(pdMS_TO_TICKS(180));
      if (!s_pat_cancel)
        drv2605l_play_effect(4);
      break;
    case PAT_RAMP:
      for (int v = 0; v <= 127 && !s_pat_cancel; v += 8) {
        drv2605l_set_rtp((uint8_t)v);
        vTaskDelay(pdMS_TO_TICKS(22));
      }
      for (int v = 127; v >= 0 && !s_pat_cancel; v -= 8) {
        drv2605l_set_rtp((uint8_t)v);
        vTaskDelay(pdMS_TO_TICKS(22));
      }
      break;
    case PAT_THROB:
      for (int c = 0; c < 3 && !s_pat_cancel; c++) {
        for (int a = 0; a < 32 && !s_pat_cancel; a++) {
          float ph = (float)a / 32.0f * 6.2831853f;
          int v = (int)((0.5f - 0.5f * cosf(ph)) * 120.0f);
          drv2605l_set_rtp((uint8_t)v);
          vTaskDelay(pdMS_TO_TICKS(18));
        }
      }
      break;
    default:
      break;
  }
  drv2605l_stop();
  s_busy = false;
  vTaskDelete(NULL);
}

static void autocal_task(void *arg) {
  (void)arg;
  drv2605l_autocal();
  drv2605l_play_effect(1);
  s_busy = false;
  vTaskDelete(NULL);
}

static void start_worker(TaskFunction_t fn, const char *name) {
  if (s_busy)
    return;
  s_busy = true;
  s_pat_cancel = false;
  if (xTaskCreatePinnedToCore(fn, name, HAPTIC_TASK_STACK_SIZE, NULL, HAPTIC_TASK_PRIORITY, NULL, SYS_CORE_UI) != pdPASS)
    s_busy = false;
}

static void haptic_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_busy) {
    if (press && ev->button == INPUT_BTN_BACK)
      s_pat_cancel = true;
    return;
  }

  int sel = menu_component_get_selected(&s_menu);

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav) {
        stop_live_intensity();
        menu_component_next(&s_menu);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        stop_live_intensity();
        menu_component_prev(&s_menu);
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press) {
        if (sel == LIVE_ROW) {
          menu_component_intensity_inc(&s_menu, LIVE_ROW);
          apply_live_intensity();
        } else if (sel == CAT_ROW) {
          s_cat = (s_cat + 1) % NCAT;
          s_fx = 0;
          menu_component_set_selector_value(&s_menu, CAT_ROW, CATS[s_cat].name);
          update_fx_label();
        } else if (sel == FX_ROW) {
          s_fx = (s_fx + 1) % CATS[s_cat].count;
          update_fx_label();
          play_current_fx();
        } else if (sel == PAT_ROW) {
          s_pat = (s_pat + 1) % PAT_COUNT;
          menu_component_set_selector_value(&s_menu, PAT_ROW, PAT_NAMES[s_pat]);
        }
      }
      break;
    case INPUT_BTN_LEFT:
      if (press) {
        if (sel == LIVE_ROW) {
          menu_component_intensity_dec(&s_menu, LIVE_ROW);
          apply_live_intensity();
        } else if (sel == CAT_ROW) {
          s_cat = (s_cat - 1 + NCAT) % NCAT;
          s_fx = 0;
          menu_component_set_selector_value(&s_menu, CAT_ROW, CATS[s_cat].name);
          update_fx_label();
        } else if (sel == FX_ROW) {
          s_fx = (s_fx - 1 + CATS[s_cat].count) % CATS[s_cat].count;
          update_fx_label();
          play_current_fx();
        } else if (sel == PAT_ROW) {
          s_pat = (s_pat - 1 + PAT_COUNT) % PAT_COUNT;
          menu_component_set_selector_value(&s_menu, PAT_ROW, PAT_NAMES[s_pat]);
        }
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        if (sel == LIVE_ROW)
          apply_live_intensity();
        else if (sel == FX_ROW)
          play_current_fx();
        else if (sel == PAT_ROW)
          start_worker(pattern_task, "haptic_pat");
        else if (sel == CAL_ROW)
          start_worker(autocal_task, "haptic_cal");
      }
      break;
    case INPUT_BTN_BACK:
      if (press) {
        stop_live_intensity();
        ui_switch_screen(SCREEN_SETTINGS);
      }
      break;
    default:
      break;
  }
}

void ui_haptic_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_rtp_live = false;
  s_cat = 0;
  s_fx = 0;
  s_pat = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "Vibration", "/assets/icons/vibration.bin");
  menu_component_add_intensity(&s_menu, "/assets/icons/graphic_eq.bin", "Live Intensity", 3);
  menu_component_add_selector(&s_menu, "/assets/icons/category.bin", "Category", CATS[0].name);
  menu_component_add_selector(&s_menu, "/assets/icons/waves.bin", "Effect", CATS[0].fx[0].name);
  menu_component_add_selector(&s_menu, "/assets/icons/pattern.bin", "Pattern", PAT_NAMES[0]);
  menu_component_add_item(&s_menu, "/assets/icons/tune.bin", "Calibrate ERM");

  ui_input_set_screen_handler(haptic_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
  ESP_LOGI(TAG, "haptic menu opened");
}
