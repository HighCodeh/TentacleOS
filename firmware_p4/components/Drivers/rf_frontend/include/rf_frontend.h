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

#ifndef RF_FRONTEND_H
#define RF_FRONTEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"

/** The LF and HF stages share one physical antenna on High Boy V2. */
typedef enum {
  RF_FRONTEND_IDLE = 0,
  RF_FRONTEND_LF_125KHZ,
  RF_FRONTEND_NFC_13MHZ,
  RF_FRONTEND_COUNT,
} rf_frontend_mode_t;

/** Claim the antenna using statically initialized synchronization. Re-claiming the current mode is
 * idempotent. */
esp_err_t rf_frontend_claim(rf_frontend_mode_t mode);

/** Release the antenna if it is owned by mode. */
void rf_frontend_release(rf_frontend_mode_t mode);

// Reserve a currently idle antenna for the calling task, including same-mode exclusion.
esp_err_t rf_frontend_reserve(rf_frontend_mode_t mode);
// Release only after the driver confirmed its field is off. A failed OFF retains the reservation.
esp_err_t rf_frontend_unreserve(rf_frontend_mode_t mode);
bool rf_frontend_can_access(rf_frontend_mode_t mode);

/** Return the currently active RF stage. */
rf_frontend_mode_t rf_frontend_get_mode(void);

const char *rf_frontend_mode_name(rf_frontend_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif // RF_FRONTEND_H
