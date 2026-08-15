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

#include "lvgl.h"

#include "battery_service.h"
#include "bq25896.h"
#include "input_manager.h"
#include "led_control.h"
#include "notify_ui.h"

// Battery discharge policy only. The screen auto-dim / auto-sleep and the
// low-battery brightness cap were removed on request: the panel stays at the
// user's configured brightness and never turns itself off. What remains is the
// battery safety net - low/critical warnings and a graceful power-off at ~2% so
// the pack is not deep-discharged. It acts on battery only (no effect on USB).

#define POWER_POLL_MS    200  // tick cadence (fine enough to time a button hold)
#define BATT_CHECK_MS    2000 // battery poll cadence (accumulated from POWER_POLL_MS)
#define POWEROFF_HOLD_MS 3000 // OK + LEFT held this long powers the device off

#define BATT_CRIT_PCT         5  // at/below: repeated critical warning
#define BATT_SHUTDOWN_PCT     2  // at/below (sustained): graceful power off
#define BATT_SHUTDOWN_SAMPLES 3  // consecutive critical polls before shutdown

static lv_timer_t *s_timer = NULL;
static bool s_low_last = false;
static bool s_crit_last = false;
static int s_shutdown_count = 0;
static uint32_t s_batt_accum_ms = 0;
static uint32_t s_poweroff_held_ms = 0;

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
    led_signal_warning();
  }
  s_low_last = bs.low;

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

// Power-off combo: hold OK + LEFT for POWEROFF_HOLD_MS. This pair is deliberate -
// BACK and LEFT are wired to the charger /QON and the P4 CHIP_PU (holding both
// would reset the P4 in hardware before firmware could act), while OK is not in
// that path, so the firmware stays alive to issue the ship-mode command.
static void check_power_combo(void) {
  if (input_is_down(INPUT_BTN_OK) && input_is_down(INPUT_BTN_LEFT)) {
    s_poweroff_held_ms += POWER_POLL_MS;
    if (s_poweroff_held_ms >= POWEROFF_HOLD_MS) {
      s_poweroff_held_ms = 0;
      notify(NOTIFY_WARNING, "Desligando...");
      bq25896_power_off(); // ship mode (no effect while on USB / VBUS present)
    }
  } else {
    s_poweroff_held_ms = 0;
  }
}

static void tick_cb(lv_timer_t *t) {
  (void)t;
  check_power_combo();

  s_batt_accum_ms += POWER_POLL_MS;
  if (s_batt_accum_ms >= BATT_CHECK_MS) {
    s_batt_accum_ms = 0;
    check_battery();
  }
}

bool power_policy_is_asleep(void) {
  return false;  // the screen never auto-sleeps anymore
}

void power_policy_init(void) {
  if (s_timer != NULL) {
    return;
  }
  s_timer = lv_timer_create(tick_cb, POWER_POLL_MS, NULL);
}
