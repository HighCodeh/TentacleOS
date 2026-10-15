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

#include "ir_file.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "ir_protocol_nec.h"
#include "ir_protocol_nec42.h"
#include "ir_protocol_samsung.h"
#include "ir_protocol_rc5.h"
#include "ir_protocol_rc6.h"
#include "ir_protocol_sony.h"
#include "ir_protocol_panasonic.h"
#include "ir_protocol_rca.h"
#include "ir_protocol_pioneer.h"

static const char *TAG = "IR_FILE";

void ir_file_init(ir_file_t *file) {
  if (file == NULL)
    return;

  file->signals = NULL;
  file->count = 0;
  file->capacity = 0;
}

void ir_file_free(ir_file_t *file) {
  if (file == NULL)
    return;

  for (size_t i = 0; i < file->count; i++) {
    free(file->signals[i].raw);
  }
  free(file->signals);
  file->signals = NULL;
  file->count = 0;
  file->capacity = 0;
}

static esp_err_t grow(ir_file_t *file) {
  if (file->count < file->capacity)
    return ESP_OK;
  size_t new_cap = (file->capacity == 0) ? IR_FILE_INITIAL_CAP : file->capacity * 2;
  ir_signal_t *tmp = heap_caps_realloc(
      file->signals, new_cap * sizeof(ir_signal_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (tmp == NULL) {
    ESP_LOGE(TAG, "Failed to grow signals array");
    return ESP_ERR_NO_MEM;
  }
  file->signals = tmp;
  file->capacity = new_cap;
  return ESP_OK;
}

static bool parse_hex_bytes(const char *str, uint32_t *out) {
  uint32_t result = 0;
  uint32_t shift = 0;
  while (*str) {
    while (isspace((unsigned char)*str))
      str++;
    if (*str == '\0')
      break;
    if (shift >= 32 || !isxdigit((unsigned char)str[0]) || !isxdigit((unsigned char)str[1]))
      return false;
    char byte_text[3] = {str[0], str[1], '\0'};
    result |= (uint32_t)strtoul(byte_text, NULL, 16) << shift;
    shift += 8;
    str += 2;
    if (*str != '\0' && !isspace((unsigned char)*str))
      return false;
  }
  if (shift == 0)
    return false;
  *out = result;
  return true;
}

static bool
flipper_to_ir_data(const char *proto, uint32_t addr, uint32_t cmd, ir_data_t *out_data) {
  memset(out_data, 0, sizeof(ir_data_t));
  if (strcmp(proto, "NEC") == 0 || strcmp(proto, "NECext") == 0) {
    out_data->protocol = IR_PROTO_NEC;
    out_data->address = addr & NEC_EXT_ADDR_MASK;
    out_data->command = cmd & NEC_EXT_CMD_MASK;
    out_data->extended = (strcmp(proto, "NECext") == 0);
    return true;
  }
  if (strcmp(proto, "NEC42") == 0 || strcmp(proto, "NEC42ext") == 0) {
    out_data->protocol = IR_PROTO_NEC42;
    out_data->address = addr & NEC42_ADDR_MASK;
    out_data->command = cmd & NEC42_CMD_MASK;
    return true;
  }
  if (strcmp(proto, "Samsung32") == 0) {
    out_data->protocol = IR_PROTO_SAMSUNG;
    out_data->address = addr & SAMSUNG_EXT_ADDR_MASK;
    out_data->command = cmd & SAMSUNG_ADDR_STANDARD_MAX;
    return true;
  }
  if (strcmp(proto, "RC6") == 0) {
    out_data->protocol = IR_PROTO_RC6;
    out_data->address = addr & RC6_ADDR_MASK;
    out_data->command = cmd & RC6_ADDR_MASK;
    return true;
  }
  if (strcmp(proto, "RC5") == 0 || strcmp(proto, "RC5X") == 0) {
    out_data->protocol = IR_PROTO_RC5;
    out_data->address = addr & RC5_ADDR_MASK;
    out_data->command = cmd & RC5_CMD_MASK;
    return true;
  }
  if (strcmp(proto, "SIRC") == 0 || strcmp(proto, "SIRC15") == 0 || strcmp(proto, "SIRC20") == 0) {
    out_data->protocol = IR_PROTO_SONY;
    out_data->address = addr & SONY_ADDR_MASK;
    out_data->command = cmd & SONY_CMD_MASK;
    out_data->nbits = (strcmp(proto, "SIRC20") == 0)   ? SONY_SIRC20_BITS
                      : (strcmp(proto, "SIRC15") == 0) ? SONY_SIRC15_BITS
                                                       : SONY_SIRC12_BITS;
    return true;
  }
  if (strcmp(proto, "Kaseikyo") == 0) {
    out_data->protocol = IR_PROTO_PANASONIC;
    out_data->address = addr;
    out_data->command = cmd & KASEIKYO_DATA_MASK;
    return true;
  }
  if (strcmp(proto, "RCA") == 0) {
    out_data->protocol = IR_PROTO_RCA;
    out_data->address = addr & RCA_ADDR_MASK;
    out_data->command = cmd & RCA_CMD_MASK;
    return true;
  }
  if (strcmp(proto, "Pioneer") == 0) {
    out_data->protocol = IR_PROTO_PIONEER;
    out_data->address = addr & PIONEER_EXT_ADDR_MASK;
    out_data->command = cmd & PIONEER_ADDR_STANDARD_MAX;
    return true;
  }
  return false;
}

static const char *to_flipper_proto(ir_protocol_t proto, uint32_t address, uint32_t command) {
  switch (proto) {
    case IR_PROTO_NEC:
      return (address > NEC_ADDR_STANDARD_MAX || command > NEC_ADDR_STANDARD_MAX) ? "NECext"
                                                                                  : "NEC";
    case IR_PROTO_NEC42:
      return "NEC42";
    case IR_PROTO_SAMSUNG:
      return "Samsung32";
    case IR_PROTO_RC6:
      return "RC6";
    case IR_PROTO_RC5:
      return (command > RC5_CMD_MASK) ? "RC5X" : "RC5";
    case IR_PROTO_PANASONIC:
      return "Kaseikyo";
    case IR_PROTO_RCA:
      return "RCA";
    case IR_PROTO_PIONEER:
      return "Pioneer";
    case IR_PROTO_SONY:
      if (address > SONY_SIRC15_ADDR_MAX)
        return "SIRC20";
      else if (address > SONY_SIRC12_ADDR_MAX)
        return "SIRC15";
      else
        return "SIRC";
    default:
      return NULL;
  }
}

static esp_err_t parse_raw_data(const char *str, ir_signal_t *sig) {
  size_t count = 0;
  const char *p = str;
  while (*p) {
    while (isspace((unsigned char)*p))
      p++;
    if (*p == '\0')
      break;
    if (!isdigit((unsigned char)*p))
      return ESP_ERR_INVALID_ARG;
    errno = 0;
    char *end;
    unsigned long duration = strtoul(p, &end, 10);
    if (errno == ERANGE || duration == 0 || duration > UINT32_MAX ||
        (*end != '\0' && !isspace((unsigned char)*end)))
      return ESP_ERR_INVALID_ARG;
    if (++count > IR_FILE_RAW_MAX_PULSES)
      return ESP_ERR_INVALID_SIZE;
    p = end;
  }

  if (count == 0)
    return ESP_ERR_INVALID_ARG;
  uint32_t *raw = heap_caps_malloc(count * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (raw == NULL) {
    ESP_LOGE(TAG, "Failed to allocate raw data");
    return ESP_ERR_NO_MEM;
  }

  p = str;
  for (size_t i = 0; i < count; i++) {
    while (isspace((unsigned char)*p))
      p++;
    char *end;
    raw[i] = (uint32_t)strtoul(p, &end, 10);
    p = end;
  }
  free(sig->raw);
  sig->raw = raw;
  sig->raw_count = count;
  return ESP_OK;
}

static esp_err_t finalize_signal(ir_signal_t *current,
                                 const char *proto_name,
                                 uint32_t addr32,
                                 uint32_t cmd32,
                                 ir_file_t *file) {
  if (!current->is_raw) {
    if (!flipper_to_ir_data(proto_name, addr32, cmd32, &current->data)) {
      ESP_LOGW(TAG, "Unknown protocol '%s' for signal '%s'", proto_name, current->name);
      free(current->raw);
      current->raw = NULL;
      return ESP_OK;
    }
    if (current->raw != NULL)
      return ESP_ERR_INVALID_ARG;
  } else if (current->raw == NULL || current->raw_count == 0 || current->frequency == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t ret = grow(file);
  if (ret != ESP_OK)
    return ret;

  file->signals[file->count++] = *current;
  current->raw = NULL; // Ownership transferred to the destination file.
  current->raw_count = 0;
  return ESP_OK;
}

esp_err_t ir_file_parse(const char *content, ir_file_t *file) {
  if (content == NULL || file == NULL)
    return ESP_ERR_INVALID_ARG;

  size_t content_len = strnlen(content, IR_FILE_CONTENT_MAX + 1U);
  if (content_len > IR_FILE_CONTENT_MAX)
    return ESP_ERR_INVALID_SIZE;

  // Allocate one full-line buffer in PSRAM. Never truncate an AC RAW frame.
  size_t max_line = 0;
  size_t line_len = 0;
  for (size_t i = 0; i < content_len; i++) {
    if (content[i] == '\n')
      line_len = 0;
    else if (++line_len > max_line)
      max_line = line_len;
  }
  char *buf = heap_caps_malloc(max_line + 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == NULL)
    return ESP_ERR_NO_MEM;

  esp_err_t ret = ESP_OK;
  const size_t initial_count = file->count;
  const char *line = content;
  ir_signal_t current;
  memset(&current, 0, sizeof(current));
  bool in_signal = false;
  bool have_type = false;
  char proto_name[IR_FILE_PROTO_NAME_MAX];
  uint32_t addr32 = 0;
  uint32_t cmd32 = 0;
  proto_name[0] = '\0';

  while (*line) {
    const char *eol = strchr(line, '\n');
    size_t len = eol ? (size_t)(eol - line) : strlen(line);

    memcpy(buf, line, len);
    buf[len] = '\0';
    line = eol ? eol + 1 : line + len;
    if (len > 0 && buf[len - 1] == '\r')
      buf[--len] = '\0';

    if (buf[0] == '#' || len == 0)
      continue;
    if (strncmp(buf, "Filetype:", 9) == 0 || strncmp(buf, "Version:", 8) == 0)
      continue;

    char *colon = strchr(buf, ':');
    if (colon == NULL)
      continue;
    *colon = '\0';
    char *key = buf;
    char *value = colon + 1;
    while (isspace((unsigned char)*value))
      value++;
    char *value_end = value + strlen(value);
    while (value_end > value && isspace((unsigned char)value_end[-1]))
      *--value_end = '\0';

    if (strcmp(key, "name") == 0) {
      if (in_signal) {
        ret = have_type ? finalize_signal(&current, proto_name, addr32, cmd32, file)
                        : ESP_ERR_INVALID_ARG;
        if (ret != ESP_OK)
          goto cleanup;
      }
      memset(&current, 0, sizeof(current));
      strncpy(current.name, value, IR_FILE_NAME_MAX - 1);
      in_signal = true;
      have_type = false;
      proto_name[0] = '\0';
      addr32 = 0;
      cmd32 = 0;
    } else if (!in_signal) {
      continue;
    } else if (strcmp(key, "type") == 0) {
      if (strcmp(value, "raw") != 0 && strcmp(value, "parsed") != 0) {
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
      }
      current.is_raw = (strcmp(value, "raw") == 0);
      have_type = true;
    } else if (strcmp(key, "protocol") == 0) {
      if (strlen(value) >= sizeof(proto_name)) {
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
      }
      strncpy(proto_name, value, sizeof(proto_name) - 1);
      proto_name[sizeof(proto_name) - 1] = '\0';
    } else if (strcmp(key, "address") == 0) {
      if (!parse_hex_bytes(value, &addr32)) {
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
      }
    } else if (strcmp(key, "command") == 0) {
      if (!parse_hex_bytes(value, &cmd32)) {
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
      }
    } else if (strcmp(key, "frequency") == 0) {
      errno = 0;
      char *end;
      unsigned long frequency = strtoul(value, &end, 10);
      while (isspace((unsigned char)*end))
        end++;
      if (!isdigit((unsigned char)*value) || errno == ERANGE || frequency == 0 ||
          frequency > UINT32_MAX || *end != '\0') {
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
      }
      current.frequency = (uint32_t)frequency;
    } else if (strcmp(key, "data") == 0) {
      ret = parse_raw_data(value, &current);
      if (ret != ESP_OK)
        goto cleanup;
    }
  }

  if (in_signal) {
    ret = have_type ? finalize_signal(&current, proto_name, addr32, cmd32, file)
                    : ESP_ERR_INVALID_ARG;
    if (ret != ESP_OK)
      goto cleanup;
  }

  ESP_LOGI(TAG, "Parsed %u signals", (unsigned)file->count);

cleanup:
  free(current.raw);
  free(buf);
  if (ret != ESP_OK) {
    while (file->count > initial_count) {
      ir_signal_t *sig = &file->signals[--file->count];
      free(sig->raw);
      memset(sig, 0, sizeof(*sig));
    }
  }
  return ret;
}

size_t ir_file_to_string(const ir_file_t *file, char *buf, size_t buf_size) {
  if (file == NULL || buf == NULL || buf_size == 0)
    return 0;

  size_t w = 0;

#define SAFE_WRITE(...)                                    \
  do {                                                     \
    if (w >= buf_size)                                     \
      return w;                                            \
    int _n = snprintf(buf + w, buf_size - w, __VA_ARGS__); \
    if (_n > 0)                                            \
      w += (size_t)_n;                                     \
  } while (0)

  SAFE_WRITE("Filetype: IR signals file\nVersion: 1\n");

  for (size_t i = 0; i < file->count; i++) {
    const ir_signal_t *sig = &file->signals[i];
    SAFE_WRITE("#\nname: %s\n", sig->name);

    if (sig->is_raw) {
      SAFE_WRITE("type: raw\nfrequency: %lu\nduty_cycle: %.6f\ndata:",
                 (unsigned long)sig->frequency,
                 (double)IR_CARRIER_DUTY_CYCLE);
      for (size_t j = 0; j < sig->raw_count; j++) {
        SAFE_WRITE(" %lu", (unsigned long)sig->raw[j]);
      }
      SAFE_WRITE("\n");
    } else {
      const char *proto =
          to_flipper_proto(sig->data.protocol, sig->data.address, sig->data.command);
      if (proto != NULL) {
        uint32_t a = sig->data.address;
        uint32_t c = sig->data.command;
        SAFE_WRITE("type: parsed\n"
                   "protocol: %s\n"
                   "address: %02lX %02lX %02lX %02lX\n"
                   "command: %02lX %02lX %02lX %02lX\n",
                   proto,
                   (unsigned long)(a & 0xFF),
                   (unsigned long)((a >> 8) & 0xFF),
                   (unsigned long)((a >> 16) & 0xFF),
                   (unsigned long)((a >> 24) & 0xFF),
                   (unsigned long)(c & 0xFF),
                   (unsigned long)((c >> 8) & 0xFF),
                   (unsigned long)((c >> 16) & 0xFF),
                   (unsigned long)((c >> 24) & 0xFF));
      } else {
        rmt_symbol_word_t symbols[IR_RMT_MEM_SYMBOLS];
        size_t count = ir_encode(&sig->data, symbols, IR_RMT_MEM_SYMBOLS);
        uint32_t freq = ir_carrier_freq(sig->data.protocol);
        SAFE_WRITE("type: raw\nfrequency: %lu\nduty_cycle: %.6f\ndata:",
                   (unsigned long)freq,
                   (double)IR_CARRIER_DUTY_CYCLE);
        for (size_t j = 0; j < count; j++) {
          SAFE_WRITE(
              " %lu %lu", (unsigned long)symbols[j].duration0, (unsigned long)symbols[j].duration1);
        }
        SAFE_WRITE("\n");
      }
    }
  }

#undef SAFE_WRITE

  return w;
}

ir_signal_t *ir_file_find(const ir_file_t *file, const char *name) {
  if (file == NULL || name == NULL)
    return NULL;

  for (size_t i = 0; i < file->count; i++) {
    if (strcmp(file->signals[i].name, name) == 0) {
      return &file->signals[i];
    }
  }
  return NULL;
}

esp_err_t ir_file_send(const ir_signal_t *signal) {
  if (signal == NULL)
    return ESP_ERR_INVALID_ARG;

  if (signal->is_raw) {
    if (signal->raw == NULL || signal->raw_count == 0 ||
        signal->frequency < IR_FILE_RAW_MIN_FREQUENCY_HZ ||
        signal->frequency > IR_FILE_RAW_MAX_FREQUENCY_HZ)
      return ESP_ERR_INVALID_ARG;
    if (signal->raw_count > IR_FILE_RAW_MAX_PULSES)
      return ESP_ERR_INVALID_SIZE;

    uint32_t total_us = 0;
    for (size_t i = 0; i < signal->raw_count; i++) {
      uint32_t duration = signal->raw[i];
      if (duration == 0 || duration > IR_FILE_RAW_MAX_DURATION_US ||
          duration > IR_FILE_RAW_MAX_TOTAL_US - total_us)
        return ESP_ERR_INVALID_SIZE;
      total_us += duration;
    }

    size_t sym_count = (signal->raw_count + 1) / 2;
    rmt_symbol_word_t *symbols = malloc(sym_count * sizeof(rmt_symbol_word_t));
    if (symbols == NULL) {
      ESP_LOGE(TAG, "Failed to allocate symbols");
      return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < sym_count; i++) {
      symbols[i].duration0 = signal->raw[i * 2];
      symbols[i].level0 = 1;
      symbols[i].duration1 = (i * 2 + 1 < signal->raw_count) ? signal->raw[i * 2 + 1] : 0;
      symbols[i].level1 = 0;
    }
    esp_err_t ret = ir_send_raw(symbols, sym_count, signal->frequency);
    free(symbols);
    return ret;
  }
  return ir_send(&signal->data);
}

esp_err_t ir_file_add_parsed(ir_file_t *file, const char *name, const ir_data_t *data) {
  if (file == NULL || name == NULL || data == NULL)
    return ESP_ERR_INVALID_ARG;

  esp_err_t ret = grow(file);
  if (ret != ESP_OK)
    return ret;

  ir_signal_t *sig = &file->signals[file->count++];
  memset(sig, 0, sizeof(ir_signal_t));
  strncpy(sig->name, name, IR_FILE_NAME_MAX - 1);
  sig->is_raw = false;
  sig->data = *data;
  return ESP_OK;
}

esp_err_t ir_file_add_raw(ir_file_t *file, const ir_file_add_raw_cfg_t *cfg) {
  if (file == NULL || cfg == NULL || cfg->name == NULL || cfg->symbols == NULL || cfg->count == 0)
    return ESP_ERR_INVALID_ARG;

  esp_err_t ret = grow(file);
  if (ret != ESP_OK)
    return ret;

  ir_signal_t *sig = &file->signals[file->count];
  memset(sig, 0, sizeof(ir_signal_t));
  strncpy(sig->name, cfg->name, IR_FILE_NAME_MAX - 1);
  sig->is_raw = true;
  sig->frequency = cfg->freq;
  sig->raw_count = cfg->count * 2;

  sig->raw =
      heap_caps_malloc(sig->raw_count * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (sig->raw == NULL) {
    ESP_LOGE(TAG, "Failed to allocate raw buffer");
    ret = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  for (size_t i = 0; i < cfg->count; i++) {
    sig->raw[i * 2] = cfg->symbols[i].duration0;
    sig->raw[i * 2 + 1] = cfg->symbols[i].duration1;
  }

  while (sig->raw_count > 0 && sig->raw[sig->raw_count - 1] == 0) {
    sig->raw_count--;
  }

  file->count++;
  return ESP_OK;

cleanup:
  memset(sig, 0, sizeof(ir_signal_t));
  return ret;
}
