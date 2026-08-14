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

/**
 * @file led_signal.h
 * @brief Semantic RGB status-LED signals driven by the LED config (g_config_led).
 *
 * Lives in Service so Service, Applications and Core can all raise a signal; it
 * reads the configured color + brightness and drives the LP5816 (Drivers). The
 * last signal wins (the LED holds that color until the next one).
 */

#ifndef LED_SIGNAL_H
#define LED_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Info / OK / idle (default purple). */
void led_signal_info(void);

/** @brief Attention: battery low, C5 link dropped, degraded mode (default yellow). */
void led_signal_warning(void);

/** @brief Fault: safe mode, required-subsystem failure (default red). */
void led_signal_error(void);

#ifdef __cplusplus
}
#endif

#endif // LED_SIGNAL_H
