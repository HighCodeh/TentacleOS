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

#include "hid_hal.h"

#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"

static const char *TAG = "HID_HAL";

// Fallback pacing used only when no readiness source is registered.
#define FALLBACK_KEY_DELAY_US   5000
#define FALLBACK_MOUSE_DELAY_US 2000
// Must exceed how long a busy host/editor can stall HID polling, else a report
// is sent into a busy endpoint and dropped. Only a real disconnect should hit it.
#define REPORT_READY_TIMEOUT_MS 1000

static hid_send_cb_t s_send_cb = NULL;
static hid_mouse_cb_t s_mouse_cb = NULL;
static hid_wait_cb_t s_wait_cb = NULL;
static hid_ready_cb_t s_ready_cb = NULL;

void hid_hal_register_callback(hid_send_cb_t send_cb,
                               hid_mouse_cb_t mouse_cb,
                               hid_wait_cb_t wait_cb,
                               hid_ready_cb_t ready_cb) {
  s_send_cb = send_cb;
  s_mouse_cb = mouse_cb;
  s_wait_cb = wait_cb;
  s_ready_cb = ready_cb;
}

// Wait until the previous report was delivered before sending the next, so we
// never overwrite an unpolled report (dropped keys). Paces to the host's ~1ms poll.
static void wait_report_ready(uint32_t fallback_us) {
  if (s_ready_cb == NULL) {
    ets_delay_us(fallback_us);
    return;
  }
  for (uint32_t waited_ms = 0; !s_ready_cb(); waited_ms++) {
    if (waited_ms >= REPORT_READY_TIMEOUT_MS) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void hid_hal_press_key(uint8_t keycode, uint8_t modifiers) {
  if (s_send_cb == NULL) {
    return;
  }

  wait_report_ready(FALLBACK_KEY_DELAY_US);
  s_send_cb(keycode, modifiers);
  wait_report_ready(FALLBACK_KEY_DELAY_US);
  s_send_cb(0, 0);
  wait_report_ready(FALLBACK_KEY_DELAY_US);
}

void hid_hal_mouse_move(int8_t x, int8_t y) {
  if (s_mouse_cb == NULL) {
    return;
  }

  wait_report_ready(FALLBACK_MOUSE_DELAY_US);
  s_mouse_cb(x, y, 0, 0);
  wait_report_ready(FALLBACK_MOUSE_DELAY_US);
}

void hid_hal_mouse_click(uint8_t buttons) {
  if (s_mouse_cb == NULL) {
    return;
  }

  wait_report_ready(FALLBACK_MOUSE_DELAY_US);
  s_mouse_cb(0, 0, buttons, 0); // Press
  wait_report_ready(FALLBACK_MOUSE_DELAY_US);
  s_mouse_cb(0, 0, 0, 0); // Release
  wait_report_ready(FALLBACK_MOUSE_DELAY_US);
}

void hid_hal_mouse_scroll(int8_t wheel) {
  if (s_mouse_cb == NULL) {
    return;
  }

  wait_report_ready(FALLBACK_MOUSE_DELAY_US);
  s_mouse_cb(0, 0, 0, wheel);
  wait_report_ready(FALLBACK_MOUSE_DELAY_US);
}

void hid_hal_wait_for_connection(void) {
  if (s_wait_cb != NULL) {
    s_wait_cb();
  }
}
