#ifndef SUBGHZ_PROTOCOL_UTILS_H
#define SUBGHZ_PROTOCOL_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/**
 * @brief Calculate absolute difference between two values
 */
static inline uint32_t subghz_abs_diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

/**
 * @brief Check if a pulse matches a target length within a tolerance percentage
 */
static inline bool subghz_check_pulse(int32_t raw_len, uint32_t target_len, uint8_t tolerance_pct) {
    uint32_t abs_len = abs(raw_len);
    uint32_t tolerance = target_len * tolerance_pct / 100;
    return (abs_len >= target_len - tolerance) && (abs_len <= target_len + tolerance);
}

#endif // SUBGHZ_PROTOCOL_UTILS_H
