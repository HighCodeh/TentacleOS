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

#include "subghz_protocol_serializer.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "cc1101.h"
#include "subghz_protocol_registry.h"

#define CAPTURE_PROTOCOL_MAX   40
#define CAPTURE_KEY_BYTES      8
#define CAPTURE_KEY_HIGH_BYTES 4
#define CAPTURE_BYTE_BITS      8
#define CAPTURE_KEY_RADIX      16

static const char *TAG = "SUBGHZ_SERIALIZER";

static const char FREQ_KEY[] = "Frequency: ";
static const char PRESET_KEY[] = "Preset: ";
static const char RAW_DATA_KEY[] = "RAW_Data:";

uint8_t subghz_protocol_get_preset_id(void) {
  return cc1101_get_active_preset_id();
}

size_t subghz_protocol_serialize_decoded(
    const subghz_data_t *data, uint32_t frequency, uint32_t te, char *out_buf, size_t out_size) {
  if (data == NULL || data->protocol_name == NULL || out_buf == NULL || out_size == 0) {
    return 0;
  }

  uint32_t val = data->raw_value;

  int written = snprintf(out_buf,
                         out_size,
                         "Filetype: High Boy SubGhz File\n"
                         "Version 1\n"
                         "Frequency: %lu\n"
                         "Preset: %u\n"
                         "Protocol: %s\n"
                         "Bit: %d\n"
                         "Key: 00 00 00 00 %02X %02X %02X %02X\n"
                         "Serial: %lu\n"
                         "Btn: %u\n"
                         "TE: %lu\n",
                         (unsigned long)frequency,
                         (unsigned int)subghz_protocol_get_preset_id(),
                         data->protocol_name,
                         (int)data->bit_count,
                         (unsigned int)((val >> 24) & 0xFF),
                         (unsigned int)((val >> 16) & 0xFF),
                         (unsigned int)((val >> 8) & 0xFF),
                         (unsigned int)(val & 0xFF),
                         (unsigned long)data->serial,
                         (unsigned int)data->btn,
                         (unsigned long)te);
  if (written < 0 || (size_t)written >= out_size) {
    out_buf[0] = '\0';
    ESP_LOGW(TAG, "Decoded capture buffer is too small");
    return 0;
  }
  return (size_t)written;
}

size_t subghz_protocol_serialize_raw(
    const int32_t *pulses, size_t count, uint32_t frequency, char *out_buf, size_t out_size) {
  if (pulses == NULL || count == 0 || out_buf == NULL || out_size == 0) {
    return 0;
  }

  int written = snprintf(out_buf,
                         out_size,
                         "Filetype: High Boy SubGhz File\n"
                         "Version 1\n"
                         "Frequency: %lu\n"
                         "Preset: %u\n"
                         "Protocol: RAW\n"
                         "RAW_Data:",
                         (unsigned long)frequency,
                         (unsigned int)subghz_protocol_get_preset_id());

  if (written < 0 || (size_t)written >= out_size) {
    out_buf[0] = '\0';
    return 0;
  }

  size_t offset = (size_t)written;
  for (size_t i = 0; i < count; i++) {
    int val_len = snprintf(out_buf + offset, out_size - offset, " %ld", (long)pulses[i]);
    if (val_len < 0 || offset + (size_t)val_len >= out_size - 1) {
      out_buf[0] = '\0';
      return 0;
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

static const char *find_field(const char *content, const char *key) {
  size_t length = strlen(key);
  const char *found = NULL;
  for (const char *line = content; *line != '\0';) {
    if (strncmp(line, key, length) == 0) {
      if (found != NULL)
        return NULL;
      found = line + length;
    }
    const char *next = strchr(line, '\n');
    if (next == NULL)
      break;
    line = next + 1;
  }
  return found;
}

static bool parse_metadata(const char *content, const char *key, uint32_t limit, uint32_t *out) {
  const char *text = find_field(content, key);
  if (text == NULL)
    return false;
  while (*text == ' ' || *text == '\t')
    ++text;
  if (!isdigit((unsigned char)*text))
    return false;
  errno = 0;
  char *end;
  unsigned long value = strtoul(text, &end, 10);
  if (errno != 0 || value > limit)
    return false;
  while (*end == ' ' || *end == '\t' || *end == '\r')
    ++end;
  if (*end != '\0' && *end != '\n')
    return false;
  *out = (uint32_t)value;
  return true;
}

size_t subghz_protocol_parse_raw(const char *content,
                                 int32_t *out_pulses,
                                 size_t max_count,
                                 uint32_t *out_frequency,
                                 uint8_t *out_preset) {
  if (content == NULL || out_pulses == NULL || max_count == 0)
    return 0;
  uint32_t frequency, preset;
  if (!parse_metadata(content, FREQ_KEY, UINT32_MAX, &frequency) ||
      !parse_metadata(content, PRESET_KEY, UINT8_MAX, &preset))
    return 0;
  const char *text = find_field(content, RAW_DATA_KEY);
  if (text == NULL)
    return 0;
  size_t count = 0;
  while (true) {
    while (isspace((unsigned char)*text))
      ++text;
    if (*text == '\0')
      break;
    if (count == max_count)
      return 0;
    errno = 0;
    char *end;
    long value = strtol(text, &end, 10);
    if (end == text || errno != 0 || value < INT32_MIN || value > INT32_MAX || value == 0 ||
        (*end != '\0' && !isspace((unsigned char)*end)))
      return 0;
    out_pulses[count++] = (int32_t)value;
    text = end;
  }
  if (count != 0) {
    if (out_frequency != NULL)
      *out_frequency = frequency;
    if (out_preset != NULL)
      *out_preset = (uint8_t)preset;
  }
  return count;
}

static bool parse_key(const char *text, uint32_t *out) {
  if (text == NULL)
    return false;
  uint32_t key = 0;
  for (size_t i = 0; i < CAPTURE_KEY_BYTES; ++i) {
    while (*text == ' ' || *text == '\t')
      ++text;
    if (!isxdigit((unsigned char)*text))
      return false;
    errno = 0;
    char *end;
    unsigned long value = strtoul(text, &end, CAPTURE_KEY_RADIX);
    if (errno != 0 || value > UINT8_MAX || (i < CAPTURE_KEY_HIGH_BYTES && value != 0))
      return false;
    if (i >= CAPTURE_KEY_HIGH_BYTES)
      key = (key << CAPTURE_BYTE_BITS) | (uint32_t)value;
    if (i + 1 < CAPTURE_KEY_BYTES && *end != ' ' && *end != '\t')
      return false;
    text = end;
  }
  while (*text == ' ' || *text == '\t' || *text == '\r')
    ++text;
  if (*text != '\0' && *text != '\n')
    return false;
  *out = key;
  return true;
}

esp_err_t subghz_protocol_parse_capture(const char *content,
                                        int32_t *out_pulses,
                                        size_t capacity,
                                        size_t *out_count,
                                        uint32_t *out_frequency,
                                        uint8_t *out_preset) {
  if (content == NULL || out_pulses == NULL || capacity == 0 || out_count == NULL ||
      out_frequency == NULL || out_preset == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_count = 0;
  uint32_t frequency = 0, preset = CC1101_PRESET_OOK_800KHZ;
  size_t count = 0;
  if (find_field(content, RAW_DATA_KEY) != NULL) {
    uint8_t raw_preset = 0;
    count = subghz_protocol_parse_raw(content, out_pulses, capacity, &frequency, &raw_preset);
    if (count == 0)
      return ESP_ERR_INVALID_ARG;
    preset = raw_preset;
  } else {
    const char *name = find_field(content, "Protocol: ");
    if (name == NULL)
      return ESP_ERR_INVALID_ARG;
    char protocol[CAPTURE_PROTOCOL_MAX];
    size_t length = strcspn(name, "\r\n");
    if (length == 0 || length >= sizeof(protocol))
      return ESP_ERR_INVALID_ARG;
    memcpy(protocol, name, length);
    protocol[length] = '\0';
    const subghz_protocol_t *encoder = subghz_protocol_registry_get_by_name(protocol);
    if (encoder == NULL || encoder->encode == NULL)
      return ESP_ERR_NOT_SUPPORTED;
    uint32_t bits = 0, serial = 0, button = 0, key = 0;
    if (!parse_metadata(content, FREQ_KEY, UINT32_MAX, &frequency) ||
        !parse_metadata(content, "Bit: ", UINT8_MAX, &bits) ||
        !parse_key(find_field(content, "Key: "), &key))
      return ESP_ERR_INVALID_ARG;
    serial = key;
    if ((strstr(content, "Preset:") != NULL &&
         !parse_metadata(content, PRESET_KEY, UINT8_MAX, &preset)) ||
        (strstr(content, "Serial:") != NULL &&
         !parse_metadata(content, "Serial: ", UINT32_MAX, &serial)) ||
        (strstr(content, "Btn:") != NULL && !parse_metadata(content, "Btn: ", UINT8_MAX, &button)))
      return ESP_ERR_INVALID_ARG;
    subghz_data_t data = {.protocol_name = protocol,
                          .raw_value = key,
                          .serial = serial,
                          .bit_count = (uint8_t)bits,
                          .btn = (uint8_t)button};
    count = subghz_protocol_registry_encode(protocol, &data, out_pulses, capacity);
    if (count == 0)
      return ESP_ERR_NOT_SUPPORTED;
  }
  *out_count = count;
  *out_frequency = frequency;
  *out_preset = (uint8_t)preset;
  return ESP_OK;
}
