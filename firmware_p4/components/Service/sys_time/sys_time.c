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

#include "sys_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "SYS_TIME";

#define SYS_TIME_NVS_NS        "systime"
#define SYS_TIME_NVS_KEY_EPOCH "last_epoch"
#define SYS_TIME_NVS_KEY_SRC   "last_src"
#define SYS_TIME_EPOCH_MIN     1735689600
#define SYS_TIME_TM_YEAR_BASE  1900

static sys_time_state_t s_state = SYS_TIME_STATE_ESTIMATED;
static sys_time_source_t s_source = SYS_TIME_SOURCE_NONE;

static void sys_time_persist(time_t epoch, sys_time_source_t source) {
  nvs_handle_t h;
  if (nvs_open(SYS_TIME_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
    return;
  }
  nvs_set_u64(h, SYS_TIME_NVS_KEY_EPOCH, (uint64_t)epoch);
  nvs_set_u8(h, SYS_TIME_NVS_KEY_SRC, (uint8_t)source);
  nvs_commit(h);
  nvs_close(h);
}

static time_t sys_time_build_epoch(void) {
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {0};
  int day = 0, year = 0, hh = 0, mm = 0, ss = 0;

  if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3 ||
      sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss) != 3) {
    return SYS_TIME_EPOCH_MIN;
  }

  const char *p = strstr(months, mon);
  struct tm tm = {0};
  tm.tm_year = year - SYS_TIME_TM_YEAR_BASE;
  tm.tm_mon = (p != NULL) ? (int)((p - months) / 3) : 0;
  tm.tm_mday = day;
  tm.tm_hour = hh;
  tm.tm_min = mm;
  tm.tm_sec = ss;
  return mktime(&tm);
}

static time_t sys_time_saved_epoch(void) {
  nvs_handle_t h;
  if (nvs_open(SYS_TIME_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
    return 0;
  }
  uint64_t saved = 0;
  if (nvs_get_u64(h, SYS_TIME_NVS_KEY_EPOCH, &saved) != ESP_OK) {
    saved = 0;
  }
  nvs_close(h);
  return (time_t)saved;
}

void sys_time_init(void) {
  setenv("TZ", "UTC0", 1);
  tzset();

  time_t baseline = sys_time_build_epoch();
  time_t saved = sys_time_saved_epoch();
  if (saved > baseline) {
    baseline = saved;
  }

  struct timeval tv = {.tv_sec = baseline, .tv_usec = 0};
  settimeofday(&tv, NULL);
  s_state = SYS_TIME_STATE_ESTIMATED;
  s_source = SYS_TIME_SOURCE_NONE;

  struct tm tm;
  gmtime_r(&baseline, &tm);
  ESP_LOGI(TAG,
           "Time service ready (estimated %04d-%02d-%02d %02d:%02d UTC)",
           tm.tm_year + SYS_TIME_TM_YEAR_BASE,
           tm.tm_mon + 1,
           tm.tm_mday,
           tm.tm_hour,
           tm.tm_min);
}

esp_err_t sys_time_set(time_t epoch, sys_time_source_t source) {
  if (epoch < SYS_TIME_EPOCH_MIN) {
    return ESP_ERR_INVALID_ARG;
  }

  struct timeval tv = {.tv_sec = epoch, .tv_usec = 0};
  if (settimeofday(&tv, NULL) != 0) {
    ESP_LOGE(TAG, "settimeofday failed");
    return ESP_FAIL;
  }

  s_state = SYS_TIME_STATE_SYNCED;
  s_source = source;
  sys_time_persist(epoch, source);

  struct tm tm;
  gmtime_r(&epoch, &tm);
  ESP_LOGI(TAG,
           "Clock set to %04d-%02d-%02d %02d:%02d:%02d UTC (source %d)",
           tm.tm_year + SYS_TIME_TM_YEAR_BASE,
           tm.tm_mon + 1,
           tm.tm_mday,
           tm.tm_hour,
           tm.tm_min,
           tm.tm_sec,
           (int)source);
  return ESP_OK;
}

sys_time_state_t sys_time_state(void) {
  return s_state;
}

sys_time_source_t sys_time_source(void) {
  return s_source;
}

time_t sys_time_now(void) {
  return time(NULL);
}

bool sys_time_format(char *out, size_t out_size, const char *fmt) {
  if (out == NULL || out_size == 0) {
    return false;
  }
  out[0] = '\0';

  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  return strftime(out, out_size, fmt, &tm) > 0;
}
