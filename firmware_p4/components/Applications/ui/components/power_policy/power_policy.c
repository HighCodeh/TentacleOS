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

#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "battery_service.h"
#include "bq25896.h"
#include "input_manager.h"
#include "notify_ui.h"
#include "power_manager.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "st7789.h"
#include "sys_prio.h"
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

// Battery thresholds (item 9e). On battery only (not charging / no VBUS):
#define BATT_DIM_PCT      10 // at/below: cap brightness to save power
#define BATT_DIM_CAP      30 // capped brightness % while battery is low
#define BATT_CRIT_PCT     5  // at/below: stronger, repeated warning
#define BATT_SHUTDOWN_PCT 2  // at/below (sustained): graceful power off
#define BATT_SHUTDOWN_SAMPLES 3 // consecutive critical polls before shutdown

typedef enum { PS_AWAKE, PS_DIM, PS_ASLEEP } power_state_t;

static lv_timer_t *s_timer = NULL;
static bool s_low_last = false;
static bool s_crit_last = false;
static bool s_batt_dim = false;     // cap brightness because the battery is low
static int s_shutdown_count = 0;
static uint32_t s_batt_accum_ms = 0;

static power_state_t s_state = PS_AWAKE;
static bool s_asleep = false;       // panel is off
static bool s_no_sleep_held = false; // whether we hold the NO_LIGHT_SLEEP lock
static int s_user_bright = 100;     // captured user level to restore on wake
static int s_cur_bright = 100;      // currently applied level (fade cursor)
static int s_target_bright = 100;   // fade destination

// Tell the C5 our power state so it can drop its radio when we are idle/asleep.
// Runs in a transient task: spi_bridge_send_command blocks on the bridge (and
// the C5 may stop/start WiFi in response), which must never stall the LVGL task.
static void power_notify_task(void *arg) {
  uint8_t state = (uint8_t)(intptr_t)arg;
  spi_bridge_send_command(SPI_ID_SYSTEM_POWER_STATE, &state, 1, NULL, NULL, 2000);
  vTaskDelete(NULL);
}

static void notify_c5_power_state(power_state_t st) {
  uint8_t spi_state = (st == PS_ASLEEP)  ? SPI_POWER_SLEEP
                      : (st == PS_DIM)   ? SPI_POWER_IDLE
                                         : SPI_POWER_ACTIVE;
  xTaskCreatePinnedToCore(power_notify_task, "pwr_notify", 3072, (void *)(intptr_t)spi_state,
                          SYS_PRIO_BACKGROUND, NULL, SYS_CORE_RADIO);
}

static void check_battery(void) {
  battery_snapshot_t bs;
  if (!battery_service_get(&bs)) {
    return;
  }

  // Only run the discharge policy on battery (not charging, no external power).
  bool on_battery = bs.present && !bs.charging && !bs.vbus_present;

  // 15%: low warning (once per entry).
  if (bs.low && !s_low_last) {
    notify(NOTIFY_WARNING, "Battery low");
  }
  s_low_last = bs.low;

  // 10%: cap brightness to stretch the remaining charge (applied in tick_cb).
  s_batt_dim = on_battery && bs.soc <= BATT_DIM_PCT;

  // 5%: stronger, repeated critical warning.
  bool crit = on_battery && bs.soc <= BATT_CRIT_PCT;
  if (crit && !s_crit_last) {
    notify(NOTIFY_WARNING, "Battery critical - charge now");
  }
  s_crit_last = crit;

  // ~2% sustained: graceful power off so the pack is not deep-discharged. Requires
  // several consecutive critical polls to avoid a single bad reading shutting down.
  if (on_battery && bs.soc <= BATT_SHUTDOWN_PCT) {
    if (++s_shutdown_count >= BATT_SHUTDOWN_SAMPLES) {
      notify(NOTIFY_WARNING, "Battery empty - shutting down");
      bq25896_power_off();  // real ship mode (no effect while on USB)
    }
  } else {
    s_shutdown_count = 0;
  }
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
  notify_c5_power_state(st);

  // Light sleep gate: hold the CPU awake while the screen is on (awake or merely
  // dimmed); release it only once the screen is fully off, so the system can
  // light-sleep when the device is idle and a button wakes it.
  if (st == PS_ASLEEP) {
    if (s_no_sleep_held) {
      power_manager_no_sleep_release();
      s_no_sleep_held = false;
    }
  } else if (!s_no_sleep_held) {
    power_manager_no_sleep_acquire();
    s_no_sleep_held = true;
  }
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

  // Low-battery brightness cap: while awake on a low battery, don't let the
  // backlight exceed BATT_DIM_CAP (dim/sleep targets already sit below it).
  if (s_batt_dim && s_state == PS_AWAKE && s_target_bright > BATT_DIM_CAP) {
    s_target_bright = BATT_DIM_CAP;
  }

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

  // Boot comes up awake: hold the CPU out of light sleep until the screen turns
  // off. enter_state() releases/re-takes this as the screen sleeps and wakes.
  if (!s_no_sleep_held) {
    power_manager_no_sleep_acquire();
    s_no_sleep_held = true;
  }

  s_timer = lv_timer_create(tick_cb, IDLE_TICK_MS, NULL);
}
