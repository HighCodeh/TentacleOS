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
 * @file gpio_header.h
 * @brief Backend for the external 18-position GPIO header (HighBoy V2).
 *
 * The header is almost entirely the board's own shared buses: SPI3 (display +
 * radios), I2C0 (charger/gauge/LED/haptic), the console UART0, plus the charger
 * CE and USB mux control lines. This driver owns the position table and drives
 * the general-purpose pins; the power rails, grounds, and the four control lines
 * (charger CE, USB mux, console UART) stay read-only and refuse set_mode/write.
 * The remaining shared-bus pins are drivable, with an on-drive warning in the UI.
 */

#ifndef GPIO_HEADER_H
#define GPIO_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief Number of physical positions on the header. */
#define GPIO_HEADER_COUNT 18

/** @brief Safety class of a header position. */
typedef enum {
  GPIO_HDR_FREE = 0, ///< genuinely free GPIO (GPIO12) — full IN/OUT/PWM, no confirm
  GPIO_HDR_PERIPH,   ///< single-signal peripheral pin, drivable only while its owner is idle
  GPIO_HDR_SPI_BUS,  ///< shared SPI3 bus — never raw GPIO
  GPIO_HDR_I2C_BUS,  ///< shared I2C0 bus — use via the I2C master only
  GPIO_HDR_CONSOLE,  ///< console UART0 + P4 strap pin — locked
  GPIO_HDR_LOCKED,   ///< dangerous (charger CE, USB mux) — locked
  GPIO_HDR_POWER,    ///< 4.2V / 3.3V rail — read-only
  GPIO_HDR_GND,      ///< ground
} gpio_hdr_class_t;

/** @brief Runtime mode of a drivable position. */
typedef enum {
  GPIO_HDR_MODE_INPUT = 0,
  GPIO_HDR_MODE_OUTPUT,
  GPIO_HDR_MODE_HIZ,
  GPIO_HDR_MODE_PWM,
} gpio_hdr_mode_t;

/** @brief Static description of one header position. */
typedef struct {
  uint8_t pin;            ///< physical position, 1..18
  int8_t gpio;            ///< P4 GPIO number, or -1 for power/ground
  gpio_hdr_class_t klass; ///< safety class
  const char *label;      ///< short id shown on the row, e.g. "GPIO12", "4.2V"
  const char *func;       ///< real function, e.g. "SPI MOSI", "Charger CE"
  const char *owner;      ///< what it is shared with (for the detail card)
} gpio_hdr_info_t;

/** @brief Reset the driver's per-pin state on screen entry. Touches no hardware:
 *         pins keep their current function until the user drives one. */
void gpio_header_begin(void);

/** @brief Number of header positions (always GPIO_HEADER_COUNT). */
int gpio_header_count(void);

/** @brief Static description of position @p idx (0..GPIO_HEADER_COUNT-1). */
const gpio_hdr_info_t *gpio_header_info(int idx);

/** @brief True if position @p idx may be driven: any GPIO except the locked
 *         control lines (charger CE, USB mux, console UART) and power/ground. */
bool gpio_header_is_actionable(int idx);

/** @brief Current mode of position @p idx. */
gpio_hdr_mode_t gpio_header_mode(int idx);

/**
 * @brief Set the mode of a drivable position.
 * @return ESP_ERR_NOT_ALLOWED on a power/ground position.
 */
esp_err_t gpio_header_set_mode(int idx, gpio_hdr_mode_t mode);

/** @brief Drive an OUTPUT position high/low. Errors unless it is OUTPUT. */
esp_err_t gpio_header_write(int idx, bool level);

/** @brief Last commanded output level of position @p idx. */
bool gpio_header_level(int idx);

/** @brief Read the live pad level (0/1), or -1 if the position is not readable. */
int gpio_header_read(int idx);

/** @brief Set the PWM duty (0..100%) on a position already in PWM mode. */
esp_err_t gpio_header_pwm_duty(int idx, uint8_t duty_pct);

/** @brief Current PWM duty (0..100%) of position @p idx. */
uint8_t gpio_header_pwm_get(int idx);

/** @brief Revert every touched pin to a safe input, stop PWM. Idempotent. */
void gpio_header_restore_all(void);

#ifdef __cplusplus
}
#endif

#endif // GPIO_HEADER_H
