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

#include "subghz_protocol_registry.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "PROTOCOL_REGISTRY";

extern subghz_protocol_t protocol_rcswitch;
extern subghz_protocol_t protocol_came;
extern subghz_protocol_t protocol_nice_flo;
extern subghz_protocol_t protocol_princeton;
extern subghz_protocol_t protocol_ansonic;
extern subghz_protocol_t protocol_chamberlain;
extern subghz_protocol_t protocol_holtek;
extern subghz_protocol_t protocol_liftmaster;
extern subghz_protocol_t protocol_linear;
extern subghz_protocol_t protocol_rossi;

static const subghz_protocol_t *REGISTERED_PROTOCOLS[] = {
    &protocol_rcswitch,
    &protocol_came,
    &protocol_nice_flo,
    &protocol_princeton,
    &protocol_ansonic,
    &protocol_chamberlain,
    &protocol_holtek,
    &protocol_liftmaster,
    &protocol_linear,
    &protocol_rossi,
};

#define REGISTERED_PROTOCOLS_COUNT (sizeof(REGISTERED_PROTOCOLS) / sizeof(REGISTERED_PROTOCOLS[0]))

void subghz_protocol_registry_init(void) {}

bool subghz_protocol_registry_decode_all(const int32_t *pulses,
                                         size_t count,
                                         subghz_data_t *out_data) {
  for (size_t i = 0; i < REGISTERED_PROTOCOLS_COUNT; i++) {
    if (REGISTERED_PROTOCOLS[i]->decode != NULL &&
        REGISTERED_PROTOCOLS[i]->decode(pulses, count, out_data)) {
      ESP_LOGD(TAG, "Decoded protocol: %s", REGISTERED_PROTOCOLS[i]->name);
      return true;
    }
  }
  return false;
}

// True if a decoded/decorated query ("CAME 12bit", "Nice Flo 12bit",
// "RCSwitch(P1)") carries the registry name as its leading token: the query
// starts with `reg` and the next character ends the token.
static bool name_matches(const char *reg, const char *query) {
  size_t n = strlen(reg);
  if (strncmp(query, reg, n) != 0) {
    return false;
  }
  char c = query[n];
  return c == '\0' || c == ' ' || c == '(';
}

const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name) {
  if (name == NULL) {
    return NULL;
  }
  for (size_t i = 0; i < REGISTERED_PROTOCOLS_COUNT; i++) {
    if (strcmp(REGISTERED_PROTOCOLS[i]->name, name) == 0) {
      return REGISTERED_PROTOCOLS[i];
    }
  }
  for (size_t i = 0; i < REGISTERED_PROTOCOLS_COUNT; i++) {
    if (name_matches(REGISTERED_PROTOCOLS[i]->name, name)) {
      return REGISTERED_PROTOCOLS[i];
    }
  }
  return NULL;
}

size_t subghz_protocol_registry_encode(const char *name,
                                       const subghz_data_t *data,
                                       int32_t *out,
                                       size_t max_count) {
  if (data == NULL || out == NULL || max_count == 0) {
    return 0;
  }
  const subghz_protocol_t *p = subghz_protocol_registry_get_by_name(name);
  if (p == NULL || p->encode == NULL) {
    return 0;
  }
  return p->encode(data, out, max_count);
}

bool subghz_protocol_registry_selftest(char *report, size_t report_len) {
  static const struct {
    const char *name;
    uint8_t bits;
    uint32_t value;
  } vectors[] = {
      {"CAME", 12, 0xABC},
      {"CAME", 24, 0xABCDEF},
      {"RCSwitch(P1)", 24, 0x123456},
      {"Ansonic", 12, 0x555},
      {"Chamberlain", 9, 0x1AB},
      {"Holtek", 12, 0xA5A},
      {"Liftmaster", 12, 0x333},
      {"Nice Flo", 12, 0x0F0},
      {"Princeton", 24, 0x0F0F0F},
      {"Linear", 10, 0x2AA},
  };

  int32_t pulses[128];
  bool all_ok = true;
  size_t off = 0;
  if (report != NULL && report_len > 0) {
    report[0] = '\0';
  }

  for (size_t v = 0; v < sizeof(vectors) / sizeof(vectors[0]); v++) {
    uint32_t mask = (vectors[v].bits >= 32) ? 0xFFFFFFFFu : ((1u << vectors[v].bits) - 1u);
    uint32_t value = vectors[v].value & mask;

    subghz_data_t in = {0};
    in.protocol_name = vectors[v].name;
    in.bit_count = vectors[v].bits;
    in.raw_value = value;
    in.serial = value;

    const subghz_protocol_t *p = subghz_protocol_registry_get_by_name(vectors[v].name);
    size_t n = subghz_protocol_registry_encode(vectors[v].name, &in, pulses, sizeof(pulses) / sizeof(pulses[0]));

    subghz_data_t out = {0};
    bool ok = (p != NULL) && (p->decode != NULL) && (n > 0) && p->decode(pulses, n, &out) &&
              out.bit_count == vectors[v].bits && (out.raw_value & mask) == value;
    if (!ok) {
      all_ok = false;
    }

    ESP_LOGI(TAG,
             "selftest %s %ubit value=0x%lX -> %s",
             vectors[v].name,
             vectors[v].bits,
             (unsigned long)value,
             ok ? "PASS" : "FAIL");
    if (report != NULL && report_len > off) {
      off += snprintf(report + off,
                      report_len - off,
                      "%-14s %2ubit 0x%06lX  %s\n",
                      vectors[v].name,
                      vectors[v].bits,
                      (unsigned long)value,
                      ok ? "PASS" : "FAIL");
    }
  }
  return all_ok;
}
