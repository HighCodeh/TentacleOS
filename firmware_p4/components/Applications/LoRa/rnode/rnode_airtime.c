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

#include "rnode_internal.h"

#include <math.h>

#include "esp_timer.h"

#define AT_SHORT_WINDOW_MS 60000UL
#define AT_LONG_WINDOW_MS  3600000UL
#define AT_RING_SLOTS      64

typedef struct {
  uint64_t end_us;
  uint32_t duration_ms;
} at_event_t;

static at_event_t s_events[AT_RING_SLOTS];
static uint8_t s_event_head = 0;
static uint8_t s_event_count = 0;
static uint16_t s_st_limit_cpct = 0;
static uint16_t s_lt_limit_cpct = 0;

static uint64_t now_us(void);
static uint32_t airtime_in_window(uint32_t window_ms);
static uint16_t pct_centi(uint32_t airtime_ms, uint32_t window_ms);

void rnode_airtime_record_tx(uint32_t airtime_ms) {
  s_events[s_event_head].end_us = now_us();
  s_events[s_event_head].duration_ms = airtime_ms;
  s_event_head = (s_event_head + 1) % AT_RING_SLOTS;
  if (s_event_count < AT_RING_SLOTS) {
    s_event_count++;
  }
}

uint16_t rnode_airtime_short(void) {
  uint32_t ms = airtime_in_window(AT_SHORT_WINDOW_MS);
  return pct_centi(ms, AT_SHORT_WINDOW_MS);
}

uint16_t rnode_airtime_long(void) {
  uint32_t ms = airtime_in_window(AT_LONG_WINDOW_MS);
  return pct_centi(ms, AT_LONG_WINDOW_MS);
}

void rnode_airtime_set_limits(uint16_t st_limit_cpct, uint16_t lt_limit_cpct) {
  s_st_limit_cpct = st_limit_cpct;
  s_lt_limit_cpct = lt_limit_cpct;
}

bool rnode_airtime_is_blocked(void) {
  if (s_st_limit_cpct > 0 && rnode_airtime_short() >= s_st_limit_cpct) {
    return true;
  }
  if (s_lt_limit_cpct > 0 && rnode_airtime_long() >= s_lt_limit_cpct) {
    return true;
  }
  return false;
}

uint32_t rnode_airtime_estimate_ms(size_t payload_bytes, uint32_t bw_hz, uint8_t sf, uint8_t cr) {
  if (bw_hz == 0 || sf < 5 || sf > 12) {
    return 0;
  }
  double rs = (double)bw_hz / (double)(1U << sf);
  double ts = 1.0 / rs;
  double preamble_symbols = 8.0 + 4.25;
  double cr_factor = (double)cr;
  double pl = (double)payload_bytes;
  double sf_d = (double)sf;
  double de = (sf >= 11) ? 1.0 : 0.0;
  double numerator = 8.0 * pl - 4.0 * sf_d + 28.0 + 16.0;
  double denominator = 4.0 * (sf_d - 2.0 * de);
  double payload_symbols = 8.0 + ceil(numerator / denominator) * cr_factor;
  if (payload_symbols < 8.0) {
    payload_symbols = 8.0;
  }
  double total_symbols = preamble_symbols + payload_symbols;
  double airtime_s = total_symbols * ts;
  double airtime_ms = airtime_s * 1000.0;
  if (airtime_ms < 1.0) {
    airtime_ms = 1.0;
  }
  return (uint32_t)(airtime_ms + 0.5);
}

static uint64_t now_us(void) {
  return (uint64_t)esp_timer_get_time();
}

static uint32_t airtime_in_window(uint32_t window_ms) {
  uint64_t cutoff_us = now_us();
  uint64_t window_us = (uint64_t)window_ms * 1000ULL;
  if (cutoff_us < window_us) {
    cutoff_us = 0;
  } else {
    cutoff_us -= window_us;
  }
  uint32_t sum_ms = 0;
  for (uint8_t i = 0; i < s_event_count; i++) {
    uint8_t idx = (s_event_head + AT_RING_SLOTS - 1 - i) % AT_RING_SLOTS;
    if (s_events[idx].end_us < cutoff_us) {
      break;
    }
    sum_ms += s_events[idx].duration_ms;
  }
  return sum_ms;
}

static uint16_t pct_centi(uint32_t airtime_ms, uint32_t window_ms) {
  if (window_ms == 0) {
    return 0;
  }
  uint64_t v = (uint64_t)airtime_ms * 10000ULL / (uint64_t)window_ms;
  if (v > 10000ULL) {
    v = 10000ULL;
  }
  return (uint16_t)v;
}
