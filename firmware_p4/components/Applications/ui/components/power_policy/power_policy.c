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
#include "notify_ui.h"

// The idle "rest screen" was removed: the backlight can't be switched on this
// prototype, so a dark overlay bought nothing but hid the UI. All that remains of
// the power policy is a one-shot low-battery warning on the falling edge.

#define BATT_CHECK_MS 2000

static lv_timer_t *s_timer = NULL;
static bool s_low_last = false;

static void tick_cb(lv_timer_t *t) {
  (void)t;
  battery_snapshot_t bs;
  if (!battery_service_get(&bs))
    return;
  if (bs.low && !s_low_last)
    notify(NOTIFY_WARNING, "Battery low");
  s_low_last = bs.low;
}

void power_policy_init(void) {
  if (s_timer != NULL)
    return;
  s_timer = lv_timer_create(tick_cb, BATT_CHECK_MS, NULL);
}
