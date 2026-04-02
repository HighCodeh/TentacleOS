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

static const char *TAG = "SUBGHZ_SERIALIZER";

static const char FREQ_KEY[] = "Frequency: ";
static const char PRESET_KEY[] = "Preset: ";
static const char RAW_DATA_KEY[] = "RAW_Data:";

uint8_t subghz_protocol_get_preset_id(void) {
  return cc1101_get_active_preset_id();
}

size_t subghz_protocol_serialize_decoded(const subghz_data_t *data, uint32_t frequency,
                                         uint32_t te, char *out_buf, size_t out_size) {
  if (data == NULL || out_buf == NULL) {
    return 0;
  }

  uint32_t val = data->raw_value;

  return (size_t)snprintf(out_buf, out_size,
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
                          (unsigned long)te);
}

size_t subghz_protocol_serialize_raw(const int32_t *pulses, size_t count,
                                     uint32_t frequency, char *out_buf, size_t out_size) {
  if (pulses == NULL || out_buf == NULL) {
    return 0;
  }

  int written = snprintf(out_buf, out_size,
                         "Filetype: High Boy SubGhz File\n"
                         "Version 1\n"
                         "Frequency: %lu\n"
                         "Preset: %u\n"
                         "Protocol: RAW\n"
                         "RAW_Data:",
                         (unsigned long)frequency,
                         (unsigned int)subghz_protocol_get_preset_id());

  if (written < 0 || (size_t)written >= out_size) {
    return 0;
  }

  size_t offset = (size_t)written;
  for (size_t i = 0; i < count; i++) {
    int val_len = snprintf(out_buf + offset, out_size - offset, " %ld", (long)pulses[i]);
    if (val_len < 0 || offset + (size_t)val_len >= out_size - 1) {
      break;
    }
    offset += (size_t)val_len;
  }

  if (offset < out_size - 1) {
    out_buf[offset] = '\n';
    offset++;
    out_buf[offset] = '\0';
  }

  return offset;
}

size_t subghz_protocol_parse_raw(const char *content, int32_t *out_pulses, size_t max_count,
                                 uint32_t *out_frequency, uint8_t *out_preset) {
  if (content == NULL || out_pulses == NULL || max_count == 0) {
    return 0;
  }

  const char *freq_ptr = strstr(content, FREQ_KEY);
  if (freq_ptr != NULL && out_frequency != NULL) {
    *out_frequency = (uint32_t)strtoul(freq_ptr + strlen(FREQ_KEY), NULL, 10);
  }

  const char *preset_ptr = strstr(content, PRESET_KEY);
  if (preset_ptr != NULL && out_preset != NULL) {
    *out_preset = (uint8_t)strtoul(preset_ptr + strlen(PRESET_KEY), NULL, 10);
  }

  const char *raw_ptr = strstr(content, RAW_DATA_KEY);
  if (raw_ptr == NULL) {
    return 0;
  }

  raw_ptr += strlen(RAW_DATA_KEY);
  size_t parsed_count = 0;
  char *end_ptr;

  while (parsed_count < max_count) {
    long val = strtol(raw_ptr, &end_ptr, 10);
    if (raw_ptr == end_ptr) {
      break;
    }
    out_pulses[parsed_count++] = (int32_t)val;
    raw_ptr = end_ptr;
  }

  return parsed_count;
}
