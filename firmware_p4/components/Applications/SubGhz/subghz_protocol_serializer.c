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

#include "subghz_protocol_serializer.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cc1101.h"

static const char* TAG = "SUBGHZ_SERIALIZER";

uint8_t subghz_protocol_get_preset_id(void) {
  return cc1101_get_active_preset_id();
}

size_t subghz_protocol_serialize_decoded(const subghz_data_t* data, uint32_t frequency, uint32_t te, char* out_buf, size_t out_size) {
  if (!data || !out_buf) return 0;

  // Format Key as 8 hex bytes (filling high bytes with 0 for uint32_t)
  uint32_t val = data->raw_value;

  return snprintf(out_buf, out_size,
                  "Filetype: High Boy SubGhz File\n"
                  "Version 1\n"
                  "Frequency: %lu\n"
                  "Preset: %u\n"
                  "Protocol: %s\n"
                  "Bit: %d\n"
                  "Key: 00 00 00 00 %02X %02X %02X %02X\n"
                  "TE: %lu\n",
                  (unsigned long)frequency,
                  (unsigned int)subghz_protocol_get_preset_id(),
                  data->protocol_name,
                  (int)data->bit_count,
                  (unsigned int)((val >> 24) & 0xFF),
                  (unsigned int)((val >> 16) & 0xFF),
                  (unsigned int)((val >> 8) & 0xFF),
                  (unsigned int)(val & 0xFF),
                  (unsigned long)te
                  );
}

size_t subghz_protocol_serialize_raw(const int32_t* pulses, size_t count, uint32_t frequency, char* out_buf, size_t out_size) {
  if (!pulses || !out_buf) return 0;

  int written = snprintf(out_buf, out_size,
                         "Filetype: High Boy SubGhz File\n"
                         "Version 1\n"
                         "Frequency: %lu\n"
                         "Preset: %u\n"
                         "Protocol: RAW\n"
                         "RAW_Data:",
                         (unsigned long)frequency,
                         (unsigned int)subghz_protocol_get_preset_id()
                         );

  if (written < 0 || (size_t)written >= out_size) return 0;

  for (size_t i = 0; i < count; i++) {
    char val_buf[16];
    int val_len = snprintf(val_buf, sizeof(val_buf), " %ld", (long)pulses[i]);

    if ((size_t)(written + val_len) >= out_size - 1) break; // Buffer full

    strcat(out_buf, val_buf);
    written += val_len;
  }

  strcat(out_buf, "\n");
  return (size_t)written + 1;
}

size_t subghz_protocol_parse_raw(const char* content, int32_t* out_pulses, size_t max_count, uint32_t* out_frequency, uint8_t* out_preset) {
  if (!content || !out_pulses || max_count == 0) return 0;

  const char* freq_ptr = strstr(content, "Frequency: ");
  if (freq_ptr && out_frequency) {
    *out_frequency = (uint32_t)strtoul(freq_ptr + 11, NULL, 10);
  }

  const char* preset_ptr = strstr(content, "Preset: ");
  if (preset_ptr && out_preset) {
    *out_preset = (uint8_t)strtoul(preset_ptr + 8, NULL, 10);
  }

  const char* raw_ptr = strstr(content, "RAW_Data:");
  if (!raw_ptr) return 0;

  raw_ptr += 9; // Skip "RAW_Data:"
  size_t count = 0;
  char* end_ptr;

  while (count < max_count) {
    long val = strtol(raw_ptr, &end_ptr, 10);
    if (raw_ptr == end_ptr) break; // End of numbers

    out_pulses[count++] = (int32_t)val;
    raw_ptr = end_ptr;
  }

  return count;
}
