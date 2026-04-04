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

#include "tos_loot.h"
#include "storage_impl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_timer.h"

static void get_date_prefix(char *buf, size_t size)
{
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);

  // If year is valid (RTC/NTP set), use YYYY-MM-DD
  if (tm.tm_year > 120) { // > 2020
    strftime(buf, size, "%Y-%m-%d", &tm);
  } else {
    // Fallback: uptime in seconds
    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    snprintf(buf, size, "boot%04lu", (unsigned long)uptime);
  }
}

void tos_loot_generate_path(const char *dir, const char *prefix, const char *ext,
                            char *out, size_t out_size,
                            char *out_name, size_t name_size)
{
  char date[20];
  get_date_prefix(date, sizeof(date));

  char filename[64];
  int idx = 1;

  while (idx < 100) {
    snprintf(filename, sizeof(filename), "%s_%s_%02d.%s", date, prefix, idx, ext);
    snprintf(out, out_size, "%s/%s", dir, filename);
    if (!storage_file_exists(out)) {
      break;
    }
    idx++;
  }

  if (out_name && name_size > 0) {
    strncpy(out_name, filename, name_size - 1);
    out_name[name_size - 1] = '\0';
  }
}
