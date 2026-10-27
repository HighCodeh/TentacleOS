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

#ifndef SUBGHZ_PROTOCOL_UTILS_H
#define SUBGHZ_PROTOCOL_UTILS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate the absolute difference between two unsigned values.
 *
 * @param a  First value.
 * @param b  Second value.
 * @return Absolute difference.
 */
static inline uint32_t subghz_abs_diff(uint32_t a, uint32_t b) {
  return (a > b) ? (a - b) : (b - a);
}

/**
 * @brief Check if a pulse matches a target length within a tolerance percentage.
 *
 * @param raw_len        Raw signed pulse duration in microseconds.
 * @param target_len     Expected unsigned pulse length in microseconds.
 * @param tolerance_pct  Tolerance as a percentage (0-100).
 * @return true if the pulse is within tolerance, false otherwise.
 */
static inline bool subghz_check_pulse(int32_t raw_len, uint32_t target_len, uint8_t tolerance_pct) {
  uint32_t abs_len = abs(raw_len);
  uint32_t tolerance = target_len * tolerance_pct / 100;
  return (abs_len >= target_len - tolerance) && (abs_len <= target_len + tolerance);
}

#define SUBGHZ_PWM_GUARD_MULT 20

/**
 * @brief Encode a fixed-code OOK-PWM signal, MSB first: bit 1 = long mark + short
 *        space, bit 0 = short mark + long space, then a trailing inter-frame guard
 *        space. This is the shared shape of CAME, Ansonic, Chamberlain, Holtek,
 *        Liftmaster, Linear, Nice Flo and Princeton.
 *
 * @param value      Bit value to transmit (MSB first).
 * @param bit_count  Number of bits (1..32).
 * @param short_us   Short element duration, microseconds.
 * @param long_us    Long element duration, microseconds.
 * @param pulses     Output buffer (us, +mark/-space).
 * @param max_count  Size of @p pulses.
 * @return Pulse count, or 0 if bit_count is out of range or the buffer is too small.
 */
static inline size_t subghz_pwm_encode(uint32_t value,
                                       uint8_t bit_count,
                                       uint32_t short_us,
                                       uint32_t long_us,
                                       int32_t *pulses,
                                       size_t max_count) {
  if (bit_count == 0 || bit_count > 32 || pulses == NULL) {
    return 0;
  }
  if (max_count < (size_t)bit_count * 2 + 1) {
    return 0;
  }
  size_t idx = 0;
  for (int i = (int)bit_count - 1; i >= 0; i--) {
    if ((value >> i) & 1u) {
      pulses[idx++] = (int32_t)long_us;
      pulses[idx++] = -(int32_t)short_us;
    } else {
      pulses[idx++] = (int32_t)short_us;
      pulses[idx++] = -(int32_t)long_us;
    }
  }
  pulses[idx++] = -(int32_t)(short_us * SUBGHZ_PWM_GUARD_MULT);
  return idx;
}

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_PROTOCOL_UTILS_H
