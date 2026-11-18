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

#include "wardrive_gps.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define GPS_FIELDS          24
#define GPS_MIN_SATS        4
#define GPS_KNOTS_TO_KMH    1.852
#define GPS_COORD_TOLERANCE 0.0001
#define GPS_DEGREES_SCALE   100.0
#define GPS_MINUTES         60.0
#define GPS_DAY_SECONDS     86400U
#define GPS_MS_SECOND       1000U
#define GPS_TIME_LIMIT      240000.0
#define GPS_NUMBER_LIMIT    1000000000.0

static bool number(const char *s, double *out) {
  if (s == NULL || *s == 0)
    return false;
  const char *p = s;
  if (*p == '-')
    p++;
  bool has_digit = false, has_dot = false;
  for (; *p; p++) {
    if (*p >= '0' && *p <= '9')
      has_digit = true;
    else if (*p == '.' && !has_dot)
      has_dot = true;
    else
      return false;
  }
  if (!has_digit)
    return false;
  char *end;
  double v = strtod(s, &end);
  if (*end || !isfinite(v) || fabs(v) > GPS_NUMBER_LIMIT)
    return false;
  *out = v;
  return true;
}
static bool coordinate(const char *s, const char *hem, bool lat, double *out) {
  double raw;
  if (!number(s, &raw) || raw < 0 || strlen(hem) != 1)
    return false;
  if (lat ? (*hem != 'N' && *hem != 'S') : (*hem != 'E' && *hem != 'W'))
    return false;
  double deg = floor(raw / GPS_DEGREES_SCALE), min = raw - deg * GPS_DEGREES_SCALE;
  double value = deg + min / GPS_MINUTES;
  if (min >= GPS_MINUTES || value > (lat ? 90.0 : 180.0))
    return false;
  *out = (*hem == 'S' || *hem == 'W') ? -value : value;
  return true;
}
static bool epoch(const char *s, uint32_t *out) {
  double raw;
  if (!number(s, &raw) || raw < 0 || raw >= GPS_TIME_LIMIT)
    return false;
  uint32_t h = (uint32_t)(raw / 10000), m = (uint32_t)(raw / 100) % 100;
  double sec = raw - h * 10000 - m * 100;
  if (h >= 24 || m >= 60 || sec >= 60)
    return false;
  *out = (uint32_t)llround((h * 3600 + m * 60 + sec) * GPS_MS_SECOND);
  return *out < GPS_DAY_SECONDS * GPS_MS_SECOND;
}
static int hex(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}
static bool date_valid(const char *s, uint32_t *out) {
  if (strlen(s) != 6)
    return false;
  uint32_t n = 0;
  for (size_t i = 0; i < 6; i++) {
    if (s[i] < '0' || s[i] > '9')
      return false;
    n = n * 10 + (uint32_t)(s[i] - '0');
  }
  unsigned d = n / 10000, m = n / 100 % 100, y = 2000 + n % 100;
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12 || d < 1 || d > days[m - 1] + (m == 2 && y % 4 == 0))
    return false;
  *out = n;
  return true;
}
static void parse(wardrive_gps_t *g, uint32_t now) {
  char *star = strchr(g->line, '*');
  if (star == NULL || strlen(star) != 3 || hex(star[1]) < 0 || hex(star[2]) < 0) {
    g->malformed++;
    return;
  }
  uint8_t check = 0;
  for (char *p = g->line + 1; p < star; p++)
    check ^= (uint8_t)*p;
  if (check != (hex(star[1]) * 16 + hex(star[2]))) {
    g->checksum_errors++;
    return;
  }
  *star = 0;
  char *f[GPS_FIELDS];
  size_t count = 0;
  f[count++] = g->line + 1;
  for (char *p = g->line + 1; *p && count < GPS_FIELDS; p++) {
    if (*p == ',') {
      *p = 0;
      f[count++] = p + 1;
    }
  }
  if (strlen(f[0]) != 5) {
    g->malformed++;
    return;
  }
  g->valid_frames++;
  g->last_frame_ms = now;
  const char *type = f[0] + 2;
  wardrive_fix_t fix = {.received_ms = now};
  double v = 0;
  if (!strcmp(type, "RMC")) {
    g->rmc.valid = false;
    if (count < 10 || strcmp(f[2], "A") || !epoch(f[1], &fix.epoch_ms) ||
        !coordinate(f[3], f[4], true, &fix.latitude) ||
        !coordinate(f[5], f[6], false, &fix.longitude) || !date_valid(f[9], &fix.date))
      return;
    if (number(f[7], &v) && v >= 0)
      fix.speed_kmh = v * GPS_KNOTS_TO_KMH;
    fix.valid = true;
    g->rmc = fix;
  } else if (!strcmp(type, "GGA")) {
    g->gga.valid = false;
    if (count < 11 || !epoch(f[1], &fix.epoch_ms))
      return;
    if (number(f[7], &v) && v >= 0 && v <= 99)
      fix.satellites = (uint8_t)v;
    if (number(f[8], &v) && v >= 0)
      fix.hdop = v;
    g->gga = fix;
    if (!number(f[6], &v) || (v != 1 && v != 2))
      return;
    fix.quality = (uint8_t)v;
    if (!coordinate(f[2], f[3], true, &fix.latitude) ||
        !coordinate(f[4], f[5], false, &fix.longitude) || !number(f[9], &fix.altitude) ||
        strcmp(f[10], "M"))
      return;
    fix.valid = true;
    g->gga = fix;
  } else if (!strcmp(type, "GSV") && count >= 4 && number(f[3], &v) && v >= 0 && v <= 99) {
    g->visible = (uint8_t)v;
  }
}
void wardrive_gps_init(wardrive_gps_t *gps) {
  memset(gps, 0, sizeof(*gps));
}
void wardrive_gps_feed(wardrive_gps_t *g, uint8_t byte, uint32_t now) {
  g->bytes++;
  if (byte == '$') {
    g->used = 0;
    g->line[g->used++] = (char)byte;
    return;
  }
  if (g->used == 0 || byte == '\r')
    return;
  if (byte == '\n') {
    g->line[g->used] = 0;
    parse(g, now);
    g->used = 0;
    return;
  }
  if (byte < 0x20 || byte > 0x7e || g->used >= sizeof(g->line) - 1) {
    g->used = 0;
    g->malformed++;
    return;
  }
  g->line[g->used++] = (char)byte;
}
bool wardrive_gps_fix(const wardrive_gps_t *g, uint32_t now, wardrive_fix_t *out) {
  memset(out, 0, sizeof(*out));
  if (!g->rmc.valid || !g->gga.valid || g->rmc.epoch_ms != g->gga.epoch_ms ||
      now - g->rmc.received_ms > WARDRIVE_GPS_STALE_MS ||
      now - g->gga.received_ms > WARDRIVE_GPS_STALE_MS || g->gga.satellites < GPS_MIN_SATS ||
      g->gga.hdop <= 0 || g->gga.hdop > WARDRIVE_GPS_MAX_HDOP ||
      fabs(g->rmc.latitude - g->gga.latitude) > GPS_COORD_TOLERANCE ||
      fabs(g->rmc.longitude - g->gga.longitude) > GPS_COORD_TOLERANCE)
    return false;
  *out = g->gga;
  out->date = g->rmc.date;
  out->speed_kmh = g->rmc.speed_kmh;
  return true;
}
