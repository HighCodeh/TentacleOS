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

#ifndef WIFI_PROBE_MON_UI_H
#define WIFI_PROBE_MON_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the WiFi probe-request monitor: a live-growing list of the
 *        client MACs nearby and the SSIDs they are probing for.
 */
void ui_wifi_probe_mon_open(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_PROBE_MON_UI_H
