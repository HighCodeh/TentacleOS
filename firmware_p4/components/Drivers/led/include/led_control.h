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

#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Initialize the RGB LED (LP5816 I2C current-sink driver).
 *
 * Assumes the shared I2C bus is already up. Resets the LP5816, enables the RGB
 * channels in manual PWM mode and starts with the LED off.
 *
 * @return
 *   - ESP_OK on success
 *   - an esp_err_t if the LP5816 does not respond on I2C
 */
esp_err_t led_rgb_init(void);

/**
 * @brief Set the LED to a specific color.
 *
 * @param r  Red intensity (0-255).
 * @param g  Green intensity (0-255).
 * @param b  Blue intensity (0-255).
 */
void led_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Release a manual preview and resume activity/power indication.
 */
void led_clear(void);

/**
 * @brief Blink the LED once with a given color and duration.
 *
 * Sets the color, waits for duration_ms, then resumes activity/power indication.
 * This function blocks for the duration of the blink.
 *
 * @param r            Red intensity (0-255).
 * @param g            Green intensity (0-255).
 * @param b            Blue intensity (0-255).
 * @param duration_ms  Blink duration in milliseconds.
 */
void led_blink(uint8_t r, uint8_t g, uint8_t b, int duration_ms);

/**
 * @brief Blink red (error indicator, 500 ms).
 */
void led_blink_red(void);

/**
 * @brief Blink green (success indicator, 220 ms).
 */
void led_blink_green(void);

/**
 * @brief Blink blue (info indicator, 500 ms).
 */
void led_blink_blue(void);

/**
 * @brief Blink purple (info indicator, 500 ms).
 */
void led_blink_purple(void);

/**
 * @brief Set the colors + global brightness used by the semantic signals.
 *
 * Pushed in from the Service layer (which owns the LED config) so the driver
 * never depends on the config module. Colors are 0xRRGGBB, brightness 0-100.
 */
void led_set_signal_config(uint32_t info, uint32_t warning, uint32_t error, int brightness);

/**
 * @brief Flash the status LED once in the configured info / warning / error
 *        color, then restore background status. Non-blocking, owned by one worker.
 */
typedef enum {
  LED_POWER_OFF,
  LED_POWER_CHARGING,
  LED_POWER_FULL,
  LED_POWER_BLOCKED,
  LED_POWER_LOW,
  LED_POWER_COUNT
} led_power_state_t;

/** Background charge pulses yield to notifications, previews and activity. */
void led_set_power_state(led_power_state_t state);
/** Backlight state; suppress recurring charge pulses while the display is lit. Task-only. */
void led_set_display_active(bool is_active);

/** Source bits allow independent activities to release only their own indication. */
#define LED_ACTIVITY_NFC    (1U << 0)
#define LED_ACTIVITY_SUBGHZ (1U << 1)
void led_set_activity(uint32_t source, bool active);
typedef struct {
  uint32_t rgb;
  uint32_t activity;
  int brightness;
  led_power_state_t power;
} led_status_t;
/** Read actual PWM registers; this is not an optical measurement. Task-only. */
esp_err_t led_get_status(led_status_t *out);

void led_signal_info(void);
void led_signal_warning(void);
void led_signal_error(void);

#ifdef __cplusplus
}
#endif

#endif // LED_CONTROL_H
