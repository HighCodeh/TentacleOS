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

#include "led_signal.h"

#include "led_control.h"
#include "tos_config.h"

static void apply_color(uint32_t hex) {
  int pct = g_config_led.brightness;
  uint8_t r = (uint8_t)(((hex >> 16) & 0xFF) * (uint32_t)pct / 100);
  uint8_t g = (uint8_t)(((hex >> 8) & 0xFF) * (uint32_t)pct / 100);
  uint8_t b = (uint8_t)((hex & 0xFF) * (uint32_t)pct / 100);
  led_set_color(r, g, b);
}

void led_signal_info(void) {
  apply_color(g_config_led.info_color);
}

void led_signal_warning(void) {
  apply_color(g_config_led.warning_color);
}

void led_signal_error(void) {
  apply_color(g_config_led.error_color);
}
