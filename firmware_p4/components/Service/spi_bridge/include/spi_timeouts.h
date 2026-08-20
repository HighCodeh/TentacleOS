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
 * @file spi_timeouts.h
 * @brief Per-command timeouts for the P4–C5 SPI bridge.
 *
 * Used by spi_bridge_get_timeout() to pick the right timeout based
 * on the command ID. Add a new constant + a case in the lookup when
 * a command needs a different timeout than the default.
 */

#ifndef SPI_TIMEOUTS_H
#define SPI_TIMEOUTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_TIMEOUT_DEFAULT_MS 1000
// WiFi scans run much slower while BLE is active (Bluetooth coexistence forces a
// longer per-channel dwell), so a full scan with a companion connected can take
// ~30 s. Keep the master's wait above that or the command times out mid-scan and
// the late response desyncs the bridge.
#define SPI_TIMEOUT_WIFI_MS 40000
// The *_BLE_INIT / *_BLE_STOP commands run the full NimBLE lifecycle on the C5
// (tear down the other GATT services via a blocking nimble_port_stop, then
// nimble_port_init + service add) synchronously on the single SPI bridge task of
// a single-core chip, so bring-up regularly overruns the 1 s default. When it
// does, the late response desyncs the bridge and the P4 keeps retrying (which
// tears the just-started service back down). Give these commands enough headroom
// to complete on the first try.
#define SPI_TIMEOUT_BLE_LIFECYCLE_MS 5000

#ifdef __cplusplus
}
#endif

#endif // SPI_TIMEOUTS_H
