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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  ir_signal_t *tmp = realloc(file->signals, new_cap * sizeof(ir_signal_t));
  if (tmp == NULL) {
    ESP_LOGE(TAG, "Failed to grow signals array");
    return ESP_ERR_NO_MEM;
  }
  file->signals = tmp;
  file->capacity = new_cap;
  return ESP_OK;
}

static uint32_t parse_hex_bytes(const char *str) {
  uint32_t result = 0;
  uint32_t shift = 0;
  while (*str && shift < 32) {
    while (*str == ' ')
      str++;
    if (*str == '\0')
      break;
    uint8_t byte;
    if (sscanf(str, "%2hhx", &byte) != 1)
      break;
    result |= ((uint32_t)byte << shift);
    shift += 8;
    str += 2;
  }
  return result;
}

static bool
flipper_to_ir_data(const char *proto, uint32_t addr, uint32_t cmd, ir_data_t *out_data) {
  memset(out_data, 0, sizeof(ir_data_t));
  if (strcmp(proto, "NEC") == 0 || strcmp(proto, "NECext") == 0) {
    out_data->protocol = IR_PROTO_NEC;
    out_data->address = addr & NEC_EXT_ADDR_MASK;
    out_data->command = cmd & NEC_EXT_CMD_MASK;
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
    return true;
  }
  if (strcmp(proto, "Kaseikyo") == 0) {
    out_data->protocol = IR_PROTO_PANASONIC;
    out_data->address = addr;
    out_data->command = cmd & 0xFF;
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
    while (*p == ' ')
      p++;
    if (*p == '\0')
      break;
    count++;
    while (*p && *p != ' ')
      p++;
  }

  sig->raw = malloc(count * sizeof(uint32_t));
  if (sig->raw == NULL) {
    ESP_LOGE(TAG, "Failed to allocate raw data");
    return ESP_ERR_NO_MEM;
  }
  sig->raw_count = 0;

  p = str;
  while (*p) {
    while (*p == ' ')
      p++;
    if (*p == '\0')
      break;
    sig->raw[sig->raw_count++] = (uint32_t)atol(p);
    while (*p && *p != ' ')
      p++;
  }
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
      return ESP_OK;
    }
  }

  esp_err_t ret = grow(file);
  if (ret != ESP_OK)
    return ret;

  file->signals[file->count++] = *current;
  return ESP_OK;
}

esp_err_t ir_file_parse(const char *content, ir_file_t *file) {
  if (content == NULL || file == NULL)
    return ESP_ERR_INVALID_ARG;

  const char *line = content;
  ir_signal_t current;
  memset(&current, 0, sizeof(current));
  bool in_signal = false;
  char proto_name[IR_FILE_PROTO_NAME_MAX];
  uint32_t addr32 = 0;
  uint32_t cmd32 = 0;
  proto_name[0] = '\0';

  while (*line) {
    const char *eol = strchr(line, '\n');
    size_t len = eol ? (size_t)(eol - line) : strlen(line);

    char buf[IR_FILE_LINE_BUF_SIZE];
    if (len >= sizeof(buf))
      len = sizeof(buf) - 1;
    memcpy(buf, line, len);
    buf[len] = '\0';
    if (len > 0 && buf[len - 1] == '\r')
      buf[--len] = '\0';

    line = eol ? eol + 1 : line + len;

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
    while (*value == ' ')
      value++;

    if (strcmp(key, "name") == 0) {
      if (in_signal) {
        esp_err_t ret = finalize_signal(&current, proto_name, addr32, cmd32, file);
        if (ret != ESP_OK)
          return ret;
      }
      memset(&current, 0, sizeof(current));
      strncpy(current.name, value, IR_FILE_NAME_MAX - 1);
      in_signal = true;
      proto_name[0] = '\0';
      addr32 = 0;
      cmd32 = 0;
    } else if (strcmp(key, "type") == 0) {
      current.is_raw = (strcmp(value, "raw") == 0);
    } else if (strcmp(key, "protocol") == 0) {
      strncpy(proto_name, value, sizeof(proto_name) - 1);
    } else if (strcmp(key, "address") == 0) {
      addr32 = parse_hex_bytes(value);
    } else if (strcmp(key, "command") == 0) {
      cmd32 = parse_hex_bytes(value);
    } else if (strcmp(key, "frequency") == 0) {
      current.frequency = (uint32_t)atol(value);
    } else if (strcmp(key, "data") == 0) {
      esp_err_t ret = parse_raw_data(value, &current);
      if (ret != ESP_OK)
        return ret;
    }
  }

  if (in_signal) {
    esp_err_t ret = finalize_signal(&current, proto_name, addr32, cmd32, file);
    if (ret != ESP_OK)
      return ret;
  }

  ESP_LOGI(TAG, "Parsed %d signals", file->count);
  return ESP_OK;
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

  sig->raw = malloc(sig->raw_count * sizeof(uint32_t));
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