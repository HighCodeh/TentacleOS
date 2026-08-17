// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MESHCORE_APP_H
#define MESHCORE_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Bring up the full MeshCore stack on the P4.
 *
 * Order of operations:
 *   1. libsodium init (provides Ed25519 sign/verify and X25519 ECDH).
 *   2. SX1262 HAL + driver init with MeshCore defaults
 *      (915 MHz, SF10, BW 250 kHz, CR 4/5, +20 dBm).
 *   3. SX1262 IRQ task.
 *   4. Load or generate Ed25519 identity (32-byte seed persisted in
 *      NVS under "id_seed"; keypair derived via crypto_sign_seed_keypair).
 *   5. meshcore core (DB, router, radio prefs) with router callbacks
 *      wired to the phoneapi push helpers.
 *   6. meshcore phoneapi (Companion protocol).
 *   7. meshcore phone bridge (SPI to C5 + BLE NUS termination).
 *   8. meshcore RX continuous.
 *   9. Spawns the meshcore_poll() task.
 *  10. Requests the C5 to start BLE advertising.
 *
 * Must be called AFTER bridge_manager_init() so the SPI bridge to the
 * C5 is ready.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_FAIL if libsodium init or keypair derivation fails
 *   - Driver error code if SX1262 init or meshcore init fails
 *   - ESP_ERR_NO_MEM if the poll task cannot be spawned
 */
esp_err_t meshcore_app_start(void);

#ifdef __cplusplus
}
#endif

#endif // MESHCORE_APP_H
