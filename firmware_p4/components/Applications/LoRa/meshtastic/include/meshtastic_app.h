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

#ifndef MESHTASTIC_APP_H
#define MESHTASTIC_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Bring up the full Meshtastic stack on the P4.
 *
 * Order of operations:
 *   1. Derive node_num from the device MAC (last 4 bytes).
 *   2. mt_nvs_init and read persisted preset/region (LONG_FAST + US
 *      defaults on first boot).
 *   3. SX1262 HAL + driver init with the preset's SF/BW/CR and the
 *      region center frequency for the primary channel slot
 *      (Meshtastic canonical default: slot 19 = 906.875 MHz on US
 *      LONG_FAST). +20 dBm TX power, 16-symbol preamble.
 *   4. SX1262 IRQ task.
 *   5. mt_pki_init (X25519 keypair via mbedTLS Curve25519).
 *   6. mt_nodedb_init (load persisted node entries).
 *   7. mt_modules_init (Admin, NodeInfo, Position, Text, Routing,
 *      TraceRoute, Telemetry, KeyVerify, NeighborInfo).
 *   8. meshtastic_mesh_init (channels, dedup, tx queue, retry table).
 *   9. phoneapi_init (Companion state machine).
 *  10. meshtastic_phone_bridge_init (SPI bridge to C5 BLE/TCP).
 *  11. meshtastic_mesh_start (arms RX continuous + spawns TX task).
 *  12. Spawns the poll task (mesh poll at 50 ms + module tick at 1 Hz).
 *  13. Requests the C5 to start BLE advertising.
 *
 * Cannot run simultaneously with meshcore_app_start: both register
 * SX1262 callbacks via sx1262_set_callbacks and would overwrite each
 * other. Only one stack at a time.
 *
 * Must be called AFTER bridge_manager_init().
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_FAIL if preset lookup or PKI init fails
 *   - Driver error code from any init step on failure
 *   - ESP_ERR_NO_MEM if the poll task cannot be spawned
 */
esp_err_t meshtastic_app_start(void);
esp_err_t meshtastic_app_stop(void);

#ifdef __cplusplus
}
#endif

#endif // MESHTASTIC_APP_H
