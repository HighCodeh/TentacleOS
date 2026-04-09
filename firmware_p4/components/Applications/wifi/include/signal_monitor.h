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

#ifndef SIGNAL_MONITOR_H
#define SIGNAL_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Start monitoring signal strength of a specific BSSID.
 *
 * @param bssid    MAC address of the target AP (6 bytes).
 * @param channel  Wi-Fi channel to monitor.
 */
void signal_monitor_start(const uint8_t *bssid, uint8_t channel);

/**
 * @brief Stop the signal monitor.
 */
void signal_monitor_stop(void);

/**
 * @brief Get the latest RSSI reading from the monitored AP.
 *
 * @return RSSI in dBm.
 */
int8_t signal_monitor_get_rssi(void);

/**
 * @brief Get time in ms since the last signal was seen.
 *
 * @return Milliseconds since last seen, or 0 if not implemented.
 */
uint32_t signal_monitor_get_last_seen_ms(void);

#ifdef __cplusplus
}
#endif

#endif // SIGNAL_MONITOR_H
