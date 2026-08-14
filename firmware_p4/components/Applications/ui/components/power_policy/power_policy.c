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

#include "power_policy.h"

#include "esp_timer.h"
#include "lvgl.h"

#include "battery_service.h"
#include "input_manager.h"
#include "notify_ui.h"
#include "st7789.h"
#include "tos_config.h"

// Screen power policy. Watches the central input activity timestamp
// (input_last_activity_ms, from input_manager) and, after auto_lock_seconds of
// no input, dims (if auto_dim) then sleeps the panel. Any input wakes it. The
// user's chosen brightness is captured before dimming and restored on wake, so
// auto-dim never overwrites it. Also keeps the one-shot low-battery warning.

#define IDLE_TICK_MS 250   // idle poll cadence (also wake latency ceiling)
#define FADE_TICK_MS 30    // tick cadence while a brightness fade is in progress
#define FADE_STEP    10    // brightness % applied per fade tick
#define DIM_LEVEL    10    // brightness % while dimmed
#define DIM_LEAD_MS  8000  // dim this long before sleeping, when auto_dim is on
#define BATT_CHECK_MS 2000 // low-battery poll cadence

typedef enum { PS_AWAKE, PS_DIM, PS_ASLEEP } power_state_t;

static lv_timer_t *s_timer = NULL;
static bool s_low_last = false;
static uint32_t s_batt_accum_ms = 0;

static power_state_t s_state = PS_AWAKE;
static bool s_asleep = false;       // panel is off
static int s_user_bright = 100;     // captured user level to restore on wake
static int s_cur_bright = 100;      // currently applied level (fade cursor)
static int s_target_bright = 100;   // fade destination

static void check_battery(void) {
  battery_snapshot_t bs;
  if (!battery_service_get(&bs)) {
    return;
  }
  if (bs.low && !s_low_last) {
    notify(NOTIFY_WARNING, "Battery low");
  }
  s_low_last = bs.low;
}

static void enter_state(power_state_t st) {
  if (st == s_state) {
    return;
  }
  if (st == PS_DIM || st == PS_ASLEEP) {
    if (s_state == PS_AWAKE) {
      // Capture the user's level once, before we start dimming.
      s_user_bright = lcd_get_brightness();
      s_cur_bright = s_user_bright;
    }
    s_target_bright = (st == PS_DIM) ? DIM_LEVEL : 0;
  } else {  // PS_AWAKE
    if (s_asleep) {
      lcd_display_sleep(false);  // wake the panel before fading the backlight up
      s_asleep = false;
    }
    s_target_bright = s_user_bright;
  }
  s_state = st;
}

static void tick_cb(lv_timer_t *t) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  uint32_t idle_ms = now_ms - input_last_activity_ms();
  uint32_t lock_ms = (uint32_t)g_config_screen.auto_lock_seconds * 1000;

  power_state_t desired = PS_AWAKE;
  if (lock_ms > 0) {
    if (idle_ms >= lock_ms) {
      desired = PS_ASLEEP;
    } else if (g_config_screen.auto_dim && lock_ms > DIM_LEAD_MS &&
               idle_ms >= lock_ms - DIM_LEAD_MS) {
      desired = PS_DIM;
    }
  }
  enter_state(desired);

  // Fade the backlight toward the target; run the timer fast while fading.
  uint32_t period = IDLE_TICK_MS;
  if (s_cur_bright != s_target_bright) {
    if (s_cur_bright < s_target_bright) {
      s_cur_bright += FADE_STEP;
      if (s_cur_bright > s_target_bright) {
        s_cur_bright = s_target_bright;
      }
    } else {
      s_cur_bright -= FADE_STEP;
      if (s_cur_bright < s_target_bright) {
        s_cur_bright = s_target_bright;
      }
    }
    lcd_apply_brightness((uint8_t)s_cur_bright);
    if (s_cur_bright == s_target_bright && s_state == PS_ASLEEP) {
      lcd_display_sleep(true);  // fully faded out: cut the panel too
      s_asleep = true;
    }
    period = FADE_TICK_MS;
  }
  lv_timer_set_period(t, period);

  s_batt_accum_ms += period;
  if (s_batt_accum_ms >= BATT_CHECK_MS) {
    s_batt_accum_ms = 0;
    check_battery();
  }
}

bool power_policy_is_asleep(void) {
  return s_asleep;
}

void power_policy_init(void) {
  if (s_timer != NULL) {
    return;
  }
  s_user_bright = lcd_get_brightness();
  s_cur_bright = s_user_bright;
  s_target_bright = s_user_bright;
  s_timer = lv_timer_create(tick_cb, IDLE_TICK_MS, NULL);
}
