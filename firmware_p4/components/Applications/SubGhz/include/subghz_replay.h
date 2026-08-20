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

#ifndef SUBGHZ_REPLAY_H
#define SUBGHZ_REPLAY_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load a saved capture and transmit it over the CC1101.
 *
 * Stops the receiver if running, initializes the transmitter, tunes to the
 * saved frequency, and replays the signal: RAW captures are sent pulse-for-pulse;
 * decoded captures are re-encoded via the protocol registry (only protocols with
 * an encoder are supported).
 *
 * @param name  Saved capture name (without .sub).
 * @return ESP_OK if the transmit was queued, ESP_ERR_NOT_SUPPORTED if the
 *         protocol cannot be re-encoded, or another error code on failure.
 */
esp_err_t subghz_replay_file(const char *name);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_REPLAY_H
