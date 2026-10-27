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

#ifndef COMMON_PORTS_H
#define COMMON_PORTS_H

#ifdef __cplusplus
extern "C" {
#endif

// Upper bound for a loaded common-ports list (kept within one SPI frame so the
// whole list reaches the C5 in a single port-scan command).
#define COMMON_PORTS_MAX 110

/**
 * @brief Load the common-ports list into @p out (up to @p max entries).
 *
 * Priority: the micro-SD file `/sdcard/wifi/common_ports.txt`, then the bundled
 * flash asset `/assets/config/wifi/common_ports.txt`, then a built-in fallback
 * list. The text files hold whitespace/comma/newline-separated port numbers;
 * lines starting with '#' are comments.
 *
 * @return number of ports loaded.
 */
int common_ports_load(int *out, int max);

#ifdef __cplusplus
}
#endif

#endif // COMMON_PORTS_H
