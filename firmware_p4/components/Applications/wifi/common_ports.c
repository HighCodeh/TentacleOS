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

#include "common_ports.h"

#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "COMMON_PORTS";

#define SD_PATH    "/sdcard/wifi/common_ports.txt"
#define FLASH_PATH "/assets/config/wifi/common_ports.txt"
#define FILE_BUF   2048

// Built-in fallback (~nmap top-100 TCP) used only if neither file is present.
static const int BUILTIN[] = {
    7,    20,   21,   22,   23,   25,   26,   37,   53,   67,   68,   69,   80,   81,   88,
    110,  111,  113,  119,  123,  135,  137,  138,  139,  143,  161,  179,  199,  389,  427,
    443,  444,  445,  465,  500,  515,  520,  548,  554,  587,  631,  636,  873,  902,  990,
    993,  995,  1025, 1080, 1099, 1194, 1433, 1434, 1521, 1723, 1883, 1900, 2049, 2082, 2083,
    2181, 2222, 2375, 2376, 3000, 3128, 3260, 3306, 3389, 3478, 3689, 4444, 5000, 5060, 5222,
    5353, 5432, 5555, 5601, 5672, 5900, 5984, 6000, 6379, 6443, 6667, 6881, 7070, 8000, 8008,
    8080, 8081, 8083, 8088, 8443, 8883, 8888, 9000, 9090, 9100, 9200, 9418, 9999, 27017};

#define BUILTIN_COUNT (int)(sizeof(BUILTIN) / sizeof(BUILTIN[0]))

// Parse whitespace/comma/newline-separated port numbers; '#' starts a comment
// that runs to end of line. Returns how many valid ports were stored.
static int parse_ports(const char *buf, int len, int *out, int max) {
  int n = 0, i = 0;
  while (i < len && n < max) {
    if (buf[i] == '#') {
      while (i < len && buf[i] != '\n')
        i++;
    } else if (buf[i] >= '0' && buf[i] <= '9') {
      int v = 0;
      while (i < len && buf[i] >= '0' && buf[i] <= '9')
        v = v * 10 + (buf[i++] - '0');
      if (v > 0 && v <= 65535)
        out[n++] = v;
    } else {
      i++;
    }
  }
  return n;
}

static int load_from(const char *path, int *out, int max) {
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return 0;
  static char buf[FILE_BUF];
  int len = (int)fread(buf, 1, sizeof(buf), f);
  fclose(f);
  if (len <= 0)
    return 0;
  int n = parse_ports(buf, len, out, max);
  if (n > 0)
    ESP_LOGI(TAG, "loaded %d ports from %s", n, path);
  return n;
}

int common_ports_load(int *out, int max) {
  int n = load_from(SD_PATH, out, max);
  if (n > 0)
    return n;
  n = load_from(FLASH_PATH, out, max);
  if (n > 0)
    return n;

  n = (BUILTIN_COUNT < max) ? BUILTIN_COUNT : max;
  for (int i = 0; i < n; i++)
    out[i] = BUILTIN[i];
  ESP_LOGI(TAG, "using %d built-in ports (no common_ports.txt found)", n);
  return n;
}
