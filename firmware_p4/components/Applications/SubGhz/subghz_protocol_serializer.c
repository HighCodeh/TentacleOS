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

#define RADIO_TEXT_SIZE   96
#define RADIO_VALUE_COUNT 4
#define RADIO_RADIX       10

#define CAPTURE_PROTOCOL_MAX      40
#define CAPTURE_KEY_BYTES         8
#define CAPTURE_KEY_HIGH_BYTES    4
#define CAPTURE_BYTE_BITS         8
#define CAPTURE_KEY_RADIX         16
#define CAPTURE_DEFAULT_REPEATS   5U
#define CAPTURE_PRINCETON_REPEATS 10U
#define CAPTURE_MAX_REPEATS       100U
#define CAPTURE_PRINCETON_GUARD   30U
#define CAPTURE_PRINCETON_TE_MAX  (INT32_MAX / 72U)

static const char *TAG = "SUBGHZ_SERIALIZER";

static const char FREQ_KEY[] = "Frequency: ";
static const char PRESET_KEY[] = "Preset: ";
static const char RAW_DATA_KEY[] = "RAW_Data:";

static bool format_radio(char *out_text, size_t size, uint8_t *out_preset);

uint8_t subghz_protocol_get_preset_id(void) {
  return cc1101_get_active_preset_id();
}

size_t subghz_protocol_serialize_decoded(
    const subghz_data_t *data, uint32_t frequency, uint32_t te, char *out_buf, size_t out_size) {
  if (data == NULL || data->protocol_name == NULL || out_buf == NULL || out_size == 0) {
    return 0;
  }

  uint32_t val = data->raw_value;
  char radio_text[RADIO_TEXT_SIZE];
  uint8_t preset;
  if (!format_radio(radio_text, sizeof(radio_text), &preset))
    return 0;
  if (data->te_us != 0)
    te = data->te_us;

  int written = snprintf(out_buf,
                         out_size,
                         "Filetype: High Boy SubGhz File\n"
                         "Version 1\n"
                         "Frequency: %lu\n"
                         "Preset: %u\n"
                         "%s"
                         "Protocol: %s\n"
                         "Bit: %d\n"
                         "Key: 00 00 00 00 %02X %02X %02X %02X\n"
                         "Serial: %lu\n"
                         "Btn: %u\n"
                         "TE: %lu\n",
                         (unsigned long)frequency,
                         (unsigned int)preset,
                         radio_text,
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
  if (strcmp(data->protocol_name, "Princeton") == 0) {
    uint32_t guard = data->guard_time == 0 ? CAPTURE_PRINCETON_GUARD : data->guard_time;
    int extra = snprintf(
        out_buf + written, out_size - (size_t)written, "Guard_time: %lu\n", (unsigned long)guard);
    if (extra < 0 || (size_t)extra >= out_size - (size_t)written) {
      out_buf[0] = '\0';
      return 0;
    }
    written += extra;
  }
  return (size_t)written;
}

size_t subghz_protocol_serialize_raw(
    const int32_t *pulses, size_t count, uint32_t frequency, char *out_buf, size_t out_size) {
  if (pulses == NULL || count == 0 || out_buf == NULL || out_size == 0) {
    return 0;
  }

  char radio_text[RADIO_TEXT_SIZE];
  uint8_t preset;
  if (!format_radio(radio_text, sizeof(radio_text), &preset))
    return 0;
  int written = snprintf(out_buf,
                         out_size,
                         "Filetype: High Boy SubGhz File\n"
                         "Version 1\n"
                         "Frequency: %lu\n"
                         "Preset: %u\n"
                         "%s"
                         "Protocol: RAW\n"
                         "RAW_Data:",
                         (unsigned long)frequency,
                         (unsigned int)preset,
                         radio_text);

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

esp_err_t subghz_protocol_parse_radio(const char *content,
                                      cc1101_preset_t preset,
                                      cc1101_radio_config_t *out_config) {
  if (content == NULL || out_config == NULL)
    return ESP_ERR_INVALID_ARG;
  cc1101_radio_config_t config;
  if (cc1101_config_from_preset(preset, &config) != ESP_OK)
    return ESP_ERR_INVALID_ARG;
  const char *text = find_field(content, "Radio:");
  if (text == NULL) {
    if (strstr(content, "Radio:") != NULL)
      return ESP_ERR_INVALID_ARG;
    *out_config = config;
    return ESP_OK;
  }
  uint8_t values[RADIO_VALUE_COUNT];
  for (size_t i = 0; i < RADIO_VALUE_COUNT; ++i) {
    while (*text == ' ' || *text == '\t')
      ++text;
    if (!isdigit((unsigned char)*text))
      return ESP_ERR_INVALID_ARG;
    errno = 0;
    char *end;
    unsigned long value = strtoul(text, &end, RADIO_RADIX);
    if (errno != 0 || value > UINT8_MAX ||
        (i + 1 < RADIO_VALUE_COUNT && *end != ' ' && *end != '\t'))
      return ESP_ERR_INVALID_ARG;
    values[i] = (uint8_t)value;
    text = end;
  }
  while (*text == ' ' || *text == '\t' || *text == '\r')
    ++text;
  if (*text != '\0' && *text != '\n')
    return ESP_ERR_INVALID_ARG;
  config.mdmcfg4 = values[0];
  config.mdmcfg3 = values[1];
  config.deviatn = values[2];
  config.agcctrl2 = values[3];
  if (cc1101_config_validate(&config) != ESP_OK)
    return ESP_ERR_INVALID_ARG;
  *out_config = config;
  return ESP_OK;
}

static bool has_field(const char *content, const char *key) {
  size_t length = strlen(key);
  for (const char *line = content; *line != '\0';) {
    if (strncmp(line, key, length) == 0)
      return true;
    const char *next = strchr(line, '\n');
    if (next == NULL)
      break;
    line = next + 1;
  }
  return false;
}

static bool field_equals(const char *text, const char *value) {
  size_t length = strlen(value);
  if (text == NULL || strncmp(text, value, length) != 0)
    return false;
  text += length;
  while (*text == ' ' || *text == '\t' || *text == '\r')
    ++text;
  return *text == '\n' || *text == '\0';
}

static bool parse_preset(const char *content, uint32_t *out_preset) {
  const char *text = find_field(content, PRESET_KEY);
  if (text == NULL)
    return false;
  while (*text == ' ' || *text == '\t')
    ++text;
  static const struct {
    const char *name;
    uint8_t preset;
  } PRESETS[] = {
      {"FuriHalSubGhzPresetOok270Async", CC1101_PRESET_OOK_270KHZ},
      {"FuriHalSubGhzPresetOok650Async", CC1101_PRESET_OOK_650KHZ},
      {"FuriHalSubGhzPreset2FSKDev238Async", CC1101_PRESET_2FSK_2KHZ},
      {"FuriHalSubGhzPreset2FSKDev476Async", CC1101_PRESET_2FSK_47KHZ},
  };
  for (size_t i = 0; i < sizeof(PRESETS) / sizeof(PRESETS[0]); ++i) {
    if (field_equals(text, PRESETS[i].name)) {
      *out_preset = PRESETS[i].preset;
      return true;
    }
  }
  return parse_metadata(content, PRESET_KEY, CC1101_PRESET_COUNT - 1U, out_preset) &&
         *out_preset != CC1101_PRESET_IDLE;
}

esp_err_t subghz_protocol_parse_repeat(const char *content, uint32_t *out_repeat) {
  if (content == NULL || out_repeat == NULL)
    return ESP_ERR_INVALID_ARG;
  if (has_field(content, RAW_DATA_KEY)) {
    *out_repeat = 1;
    return ESP_OK;
  }
  uint32_t repeat = field_equals(find_field(content, "Protocol: "), "Princeton")
                        ? CAPTURE_PRINCETON_REPEATS
                        : CAPTURE_DEFAULT_REPEATS;
  if (has_field(content, "Repeat:") &&
      (!parse_metadata(content, "Repeat: ", CAPTURE_MAX_REPEATS, &repeat) || repeat == 0))
    return ESP_ERR_INVALID_ARG;
  *out_repeat = repeat;
  return ESP_OK;
}

size_t subghz_protocol_parse_raw(const char *content,
                                 int32_t *out_pulses,
                                 size_t max_count,
                                 uint32_t *out_frequency,
                                 uint8_t *out_preset) {
  if (content == NULL || out_pulses == NULL || max_count == 0)
    return 0;
  uint32_t frequency, preset;
  if (!parse_metadata(content, FREQ_KEY, UINT32_MAX, &frequency) || !parse_preset(content, &preset))
    return 0;
  cc1101_radio_config_t radio;
  if (subghz_protocol_parse_radio(content, (cc1101_preset_t)preset, &radio) != ESP_OK)
    return 0;
  size_t count = 0;
  bool has_raw = false;
  for (const char *line = content; *line != '\0';) {
    const char *end_line = strchr(line, '\n');
    if (end_line == NULL)
      end_line = line + strlen(line);
    bool is_raw = strncmp(line, RAW_DATA_KEY, sizeof(RAW_DATA_KEY) - 1U) == 0;
    const char *text = is_raw ? line + sizeof(RAW_DATA_KEY) - 1U : line;
    if (is_raw)
      has_raw = true;
    else if (has_raw) {
      while (text < end_line && isspace((unsigned char)*text))
        ++text;
      if (text != end_line && *text != '#')
        return 0;
    }
    if (is_raw) {
      size_t before = count;
      while (text < end_line) {
        while (text < end_line && isspace((unsigned char)*text))
          ++text;
        if (text == end_line)
          break;
        if (count == max_count)
          return 0;
        errno = 0;
        char *end;
        long value = strtol(text, &end, 10);
        if (end == text || end > end_line || errno != 0 || value <= INT32_MIN ||
            value > INT32_MAX || value == 0 || (end < end_line && !isspace((unsigned char)*end)))
          return 0;
        out_pulses[count++] = (int32_t)value;
        text = end;
      }
      if (count == before)
        return 0;
    }
    if (*end_line == '\0')
      break;
    line = end_line + 1;
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
  if (has_field(content, RAW_DATA_KEY)) {
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
    if ((has_field(content, "Preset:") && !parse_preset(content, &preset)) ||
        (strstr(content, "Serial:") != NULL &&
         !parse_metadata(content, "Serial: ", UINT32_MAX, &serial)) ||
        (strstr(content, "Btn:") != NULL && !parse_metadata(content, "Btn: ", UINT8_MAX, &button)))
      return ESP_ERR_INVALID_ARG;
    subghz_data_t data = {.protocol_name = protocol,
                          .raw_value = key,
                          .serial = serial,
                          .bit_count = (uint8_t)bits,
                          .btn = (uint8_t)button};
    if (encoder->name != NULL &&
        (strcmp(encoder->name, "Princeton") == 0 || strcmp(encoder->name, "RCSwitch") == 0)) {
      if ((has_field(content, "TE:") &&
           (!parse_metadata(content, "TE: ", CAPTURE_PRINCETON_TE_MAX, &data.te_us) ||
            data.te_us == 0)) ||
          (has_field(content, "Guard_time:") &&
           !parse_metadata(content, "Guard_time: ", UINT32_MAX, &data.guard_time)))
        return ESP_ERR_INVALID_ARG;
    }
    count = subghz_protocol_registry_encode(protocol, &data, out_pulses, capacity);
    if (count == 0)
      return ESP_ERR_NOT_SUPPORTED;
  }
  cc1101_radio_config_t radio;
  if (subghz_protocol_parse_radio(content, (cc1101_preset_t)preset, &radio) != ESP_OK)
    return ESP_ERR_INVALID_ARG;
  *out_count = count;
  *out_frequency = frequency;
  *out_preset = (uint8_t)preset;
  return ESP_OK;
}

static bool format_radio(char *out_text, size_t size, uint8_t *out_preset) {
  cc1101_radio_config_t radio;
  if (cc1101_get_radio_config(&radio) != ESP_OK) {
    ESP_LOGW(TAG, "Cannot read capture radio settings");
    return false;
  }
  *out_preset = radio.preset;
  out_text[0] = '\0';
  if (!cc1101_config_is_custom(&radio))
    return true;
  int written = snprintf(out_text,
                         size,
                         "Radio: %u %u %u %u\n",
                         radio.mdmcfg4,
                         radio.mdmcfg3,
                         radio.deviatn,
                         radio.agcctrl2);
  return written > 0 && (size_t)written < size;
}
