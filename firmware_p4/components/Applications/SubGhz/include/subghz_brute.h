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

#ifndef SUBGHZ_BRUTE_H
#define SUBGHZ_BRUTE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Progress of a running brute-force sweep.
 */
typedef struct {
  uint32_t sent;  /**< @brief Codes transmitted so far. */
  uint32_t total; /**< @brief Total codes in the sweep. */
  bool running;   /**< @brief true while the sweep is active. */
  bool done;      /**< @brief true once the sweep finished (or was stopped). */
} subghz_brute_status_t;

/**
 * @brief Start a real brute-force sweep: transmit every code for a protocol.
 *
 * Stops the receiver, initializes the transmitter, tunes to @p freq, then
 * transmits each candidate code. There is no "hit" detection on fixed-code
 * remotes — the sweep simply completes.
 *
 * @param protocol   Protocol name (must have an encoder: e.g. "CAME", "RCSwitch").
 * @param bit_count  Code width in bits (sweep size is capped internally).
 * @param freq       Transmit frequency in Hz.
 * @return ESP_OK if the sweep started, or an error code on failure.
 */
esp_err_t subghz_brute_start(const char *protocol, uint8_t bit_count, uint32_t freq);

/**
 * @brief Stop a running brute-force sweep.
 */
void subghz_brute_stop(void);

/**
 * @brief Copy the current sweep progress.
 *
 * @param out  Destination.
 * @return true if a sweep has been started (out is filled), false otherwise.
 */
bool subghz_brute_get_status(subghz_brute_status_t *out);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_BRUTE_H
