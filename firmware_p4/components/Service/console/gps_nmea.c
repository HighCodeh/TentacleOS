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

#include "gps_nmea.h"

#include <stdbool.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "GPS_NMEA";
#endif

#define NMEA_FIELD_COUNT       15
#define NMEA_COORD_BASE        100
#define NMEA_MINUTES           60
#define NMEA_LAT_MAX           90
#define NMEA_LON_MAX           180
#define NMEA_TYPE_OFFSET       3
#define NMEA_NAME_LENGTH       6
#define NMEA_CHECKSUM_DIGITS   2
#define NMEA_QUALITY_MAX       8
#define NMEA_QUALITY_ESTIMATED 6
#define NMEA_DECIMAL_BASE      10
#define NMEA_HEX_SHIFT         4
#define NMEA_LAT_DIGITS        4
#define NMEA_LON_DIGITS        5
#define NMEA_FRACTION_SCALE    1000000
#define NMEA_SAT_DIGITS        2

enum {
  GPS_NMEA_NAME = 0,
  GPS_NMEA_UTC,
  GPS_NMEA_LAT,
  GPS_NMEA_NS,
  GPS_NMEA_LON,
  GPS_NMEA_EW,
  GPS_NMEA_QUALITY,
  GPS_NMEA_SATS,
  GPS_NMEA_HDOP,
  GPS_NMEA_ALT,
  GPS_NMEA_ALT_UNIT,
  GPS_NMEA_GEOID,
  GPS_NMEA_GEOID_UNIT,
  GPS_NMEA_AGE,
  GPS_NMEA_STATION,
  GPS_NMEA_COUNT
};

static int hex_value(char ch) {
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'A' && ch <= 'F')
    return ch - 'A' + NMEA_DECIMAL_BASE;
  if (ch >= 'a' && ch <= 'f')
    return ch - 'a' + NMEA_DECIMAL_BASE;
  return -1;
}

static bool coordinate(const char *value, const char *hemisphere, bool is_lat, double *out_value) {
  if (!*value || strlen(hemisphere) != 1)
    return false;
  unsigned whole = 0;
  unsigned digits = 0;
  while (*value >= '0' && *value <= '9') {
    if (++digits > NMEA_LON_DIGITS)
      return false;
    whole = whole * NMEA_DECIMAL_BASE + *value++ - '0';
  }
  if (digits != (is_lat ? NMEA_LAT_DIGITS : NMEA_LON_DIGITS))
    return false;
  unsigned fraction = 0, scale = 1;
  if (*value == '.') {
    value++;
    if (!*value)
      return false;
    while (*value >= '0' && *value <= '9') {
      if (scale < NMEA_FRACTION_SCALE) {
        fraction = fraction * NMEA_DECIMAL_BASE + *value - '0';
        scale *= NMEA_DECIMAL_BASE;
      }
      value++;
    }
  }
  if (*value)
    return false;
  unsigned degrees = whole / NMEA_COORD_BASE;
  unsigned minute_whole = whole % NMEA_COORD_BASE;
  unsigned limit = is_lat ? NMEA_LAT_MAX : NMEA_LON_MAX;
  if (minute_whole >= NMEA_MINUTES || degrees > limit ||
      (degrees == limit && (minute_whole || fraction)))
    return false;
  double minutes = minute_whole + (double)fraction / scale;
  char positive = is_lat ? 'N' : 'E';
  char negative = is_lat ? 'S' : 'W';
  if (*hemisphere != positive && *hemisphere != negative)
    return false;
  *out_value = (degrees + minutes / NMEA_MINUTES) * (*hemisphere == negative ? -1 : 1);
  return true;
}

int gps_nmea_parse(char *line, gps_nmea_fix_t *out_fix) {
  if (!line || !out_fix)
    return -1;
  memset(out_fix, 0, sizeof(*out_fix));
  if (*line != '$')
    return -1;
  char *star = strchr(line, '*');
  if (!star || strlen(star + 1) != NMEA_CHECKSUM_DIGITS)
    return -1;
  int high = hex_value(star[1]), low = hex_value(star[2]);
  if (high < 0 || low < 0)
    return -1;
  uint8_t checksum = 0;
  for (char *cursor = line + 1; cursor < star; cursor++)
    checksum ^= (uint8_t)*cursor;
  if (checksum != (high << NMEA_HEX_SHIFT | low)) {
#ifdef ESP_PLATFORM
    ESP_LOGD(TAG, "NMEA checksum mismatch");
#endif
    return -1;
  }
  *star = 0;
  char *fields[NMEA_FIELD_COUNT];
  unsigned count = 0;
  char *cursor = line;
  do {
    fields[count++] = cursor;
    char *comma = strchr(cursor, ',');
    if (!comma)
      break;
    *comma = 0;
    cursor = comma + 1;
  } while (count < NMEA_FIELD_COUNT);
  if (count < NMEA_FIELD_COUNT || strlen(fields[GPS_NMEA_NAME]) != NMEA_NAME_LENGTH ||
      strcmp(fields[GPS_NMEA_NAME] + NMEA_TYPE_OFFSET, "GGA"))
    return -1;
  if (strlen(fields[GPS_NMEA_QUALITY]) != 1 || fields[GPS_NMEA_QUALITY][0] < '0' ||
      fields[GPS_NMEA_QUALITY][0] > '0' + NMEA_QUALITY_MAX)
    return -1;
  out_fix->quality = fields[GPS_NMEA_QUALITY][0] - '0';
  if (strlen(fields[GPS_NMEA_SATS]) != NMEA_SAT_DIGITS || fields[GPS_NMEA_SATS][0] < '0' ||
      fields[GPS_NMEA_SATS][0] > '9' || fields[GPS_NMEA_SATS][1] < '0' ||
      fields[GPS_NMEA_SATS][1] > '9')
    return -1;
  out_fix->satellites =
      (fields[GPS_NMEA_SATS][0] - '0') * NMEA_DECIMAL_BASE + fields[GPS_NMEA_SATS][1] - '0';
  const unsigned indices[] = {GPS_NMEA_UTC, GPS_NMEA_HDOP, GPS_NMEA_ALT};
  char *outputs[] = {out_fix->utc, out_fix->hdop, out_fix->altitude};
  for (unsigned i = 0; i < sizeof(indices) / sizeof(indices[0]); i++) {
    if (strlen(fields[indices[i]]) >= GPS_NMEA_VALUE_SIZE)
      return -1;
    strcpy(outputs[i], fields[indices[i]]);
  }
  if (!out_fix->quality || out_fix->quality >= NMEA_QUALITY_ESTIMATED)
    return 0;
  if (!coordinate(fields[GPS_NMEA_LAT], fields[GPS_NMEA_NS], true, &out_fix->latitude) ||
      !coordinate(fields[GPS_NMEA_LON], fields[GPS_NMEA_EW], false, &out_fix->longitude))
    return -1;
  return 1;
}
