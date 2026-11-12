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

#ifndef GPS_NMEA_H
#define GPS_NMEA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPS_NMEA_LINE_SIZE  128
#define GPS_NMEA_VALUE_SIZE 16

typedef struct {
  double latitude;
  double longitude;
  unsigned quality;
  unsigned satellites;
  char utc[GPS_NMEA_VALUE_SIZE];
  char hdop[GPS_NMEA_VALUE_SIZE];
  char altitude[GPS_NMEA_VALUE_SIZE];
} gps_nmea_fix_t;

// Consumes a mutable, NUL-terminated sentence. -1 invalid/other, 0 no fix, 1 fix.
int gps_nmea_parse(char *line, gps_nmea_fix_t *out_fix);

#ifdef __cplusplus
}
#endif
#endif
