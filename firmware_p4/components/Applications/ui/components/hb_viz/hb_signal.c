// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hb_signal.h"

// Color stops along the RSSI axis, strong -> weak. Between two adjacent stops
// the channels are linearly interpolated; outside the ends the value is
// clamped. Tuned so a typical "good" AP (>= -55) is clearly green and a barely
// visible one (<= -85) is clearly red.
typedef struct {
  int rssi;
  uint8_t r, g, b;
} signal_stop_t;

static const signal_stop_t SIGNAL_STOPS[] = {
    {-50, 0x2E, 0xCC, 0x40}, // strong  - green
    {-63, 0xB6, 0xD9, 0x0B}, // good    - lime
    {-72, 0xFF, 0xC1, 0x07}, // fair    - amber
    {-82, 0xFF, 0x8A, 0x00}, // weak    - orange
    {-92, 0xE5, 0x39, 0x35}, // faint   - red
};

#define SIGNAL_STOP_COUNT ((int)(sizeof(SIGNAL_STOPS) / sizeof(SIGNAL_STOPS[0])))

static uint8_t lerp_u8(uint8_t a, uint8_t b, int num, int den) {
  if (den <= 0)
    return a;
  return (uint8_t)(a + ((int)b - (int)a) * num / den);
}

lv_color_t hb_signal_color(int rssi) {
  if (rssi >= SIGNAL_STOPS[0].rssi)
    return lv_color_make(SIGNAL_STOPS[0].r, SIGNAL_STOPS[0].g, SIGNAL_STOPS[0].b);
  int last = SIGNAL_STOP_COUNT - 1;
  if (rssi <= SIGNAL_STOPS[last].rssi)
    return lv_color_make(SIGNAL_STOPS[last].r, SIGNAL_STOPS[last].g, SIGNAL_STOPS[last].b);

  for (int i = 0; i < last; i++) {
    const signal_stop_t *hi = &SIGNAL_STOPS[i];     // higher (stronger) rssi
    const signal_stop_t *lo = &SIGNAL_STOPS[i + 1]; // lower (weaker) rssi
    if (rssi <= hi->rssi && rssi >= lo->rssi) {
      int span = hi->rssi - lo->rssi; // positive
      int num = hi->rssi - rssi;      // 0 at hi -> span at lo
      return lv_color_make(lerp_u8(hi->r, lo->r, num, span),
                           lerp_u8(hi->g, lo->g, num, span),
                           lerp_u8(hi->b, lo->b, num, span));
    }
  }
  // Unreachable given the clamps above, but keep the compiler happy.
  return lv_color_make(SIGNAL_STOPS[last].r, SIGNAL_STOPS[last].g, SIGNAL_STOPS[last].b);
}

int hb_signal_quality(int rssi) {
  int q = (rssi + 100) * 2;
  if (q < 0)
    q = 0;
  if (q > 100)
    q = 100;
  return q;
}

int hb_signal_bars(int rssi) {
  if (rssi >= -55)
    return 4;
  if (rssi >= -67)
    return 3;
  if (rssi >= -78)
    return 2;
  if (rssi >= -88)
    return 1;
  return 0;
}
