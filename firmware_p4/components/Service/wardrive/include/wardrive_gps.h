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

#ifndef WARDRIVE_GPS_H
#define WARDRIVE_GPS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WARDRIVE_NMEA_BYTES   128
#define WARDRIVE_GPS_STALE_MS 3000U
#define WARDRIVE_GPS_MAX_HDOP 10.0

typedef struct {
  double latitude, longitude, altitude, hdop, speed_kmh;
  uint32_t epoch_ms, date, received_ms;
  uint8_t satellites, quality;
  bool valid;
} wardrive_fix_t;
typedef struct {
  char line[WARDRIVE_NMEA_BYTES];
  size_t used;
  uint32_t bytes, valid_frames, checksum_errors, malformed, last_frame_ms;
  uint8_t visible;
  wardrive_fix_t rmc, gga;
} wardrive_gps_t;
void wardrive_gps_init(wardrive_gps_t *gps);
void wardrive_gps_feed(wardrive_gps_t *gps, uint8_t byte, uint32_t now_ms);
bool wardrive_gps_fix(const wardrive_gps_t *gps, uint32_t now_ms, wardrive_fix_t *out_fix);
#ifdef __cplusplus
}
#endif
#endif
