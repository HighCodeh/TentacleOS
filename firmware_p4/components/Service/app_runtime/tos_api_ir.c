// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tos_api.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"

#include "ir.h"
#include "ir_ac.h"
#include "ir_file.h"
#include "ir_lease.h"
#include "host_link_local_ir.h"
#include "host_link_local_rf.h"
#include "spi_protocol.h"
#include "tos_app_ctx.h"

#define IR_LOCK_MS        3000
#define IR_TIMEOUT_SIZE   2
#define IR_BYTE_BITS      8
#define IR_ADDRESS_OFFSET 1
#define IR_COMMAND_OFFSET 5
#define IR_REPEAT_OFFSET  9

static const char *TAG = "TOS_IR";

static esp_err_t execute(host_rf_request_t *request, uint32_t capability) {
  if (!tos_app_cap_check(capability))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(IR_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t error =
      tos_api_get()->should_stop() ? ESP_ERR_INVALID_STATE : host_local_rf_execute(request);
  host_local_rf_unlock();
  if (error != ESP_OK)
    ESP_LOGD(TAG, "IR operation ended: %s", esp_err_to_name(error));
  return error;
}

static esp_err_t receive_code(tos_ir_code_t *out, uint32_t timeout_ms) {
  if (out == NULL || timeout_ms == 0 || timeout_ms > HOST_LOCAL_IR_TIMEOUT_MAX_MS)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  uint8_t payload[IR_TIMEOUT_SIZE] = {(uint8_t)timeout_ms, (uint8_t)(timeout_ms >> IR_BYTE_BITS)};
  uint8_t response[HOST_RF_RESPONSE_MAX] = {0};
  host_rf_request_t request = {.command = SPI_ID_IR_RX_FRAME,
                               .payload = payload,
                               .length = sizeof(payload),
                               .out = response,
                               .capacity = sizeof(response)};
  esp_err_t error = execute(&request, TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return error;
  if (request.out_length != HOST_LOCAL_IR_CODE_SIZE || response[0] > TOS_IR_NEC42 ||
      response[IR_REPEAT_OFFSET] > 1)
    return ESP_ERR_INVALID_SIZE;
  out->protocol = response[0];
  memcpy(&out->address, response + IR_ADDRESS_OFFSET, sizeof(out->address));
  memcpy(&out->command, response + IR_COMMAND_OFFSET, sizeof(out->command));
  out->repeat = response[IR_REPEAT_OFFSET] != 0;
  return ESP_OK;
}

static esp_err_t send_code(const tos_ir_code_t *code) {
  if (code == NULL || code->protocol > TOS_IR_NEC42)
    return ESP_ERR_INVALID_ARG;
  uint8_t payload[HOST_LOCAL_IR_CODE_SIZE] = {code->protocol};
  memcpy(payload + IR_ADDRESS_OFFSET, &code->address, sizeof(code->address));
  memcpy(payload + IR_COMMAND_OFFSET, &code->command, sizeof(code->command));
  payload[IR_REPEAT_OFFSET] = code->repeat;
  uint8_t response[HOST_RF_RESPONSE_MAX];
  host_rf_request_t request = {.command = SPI_ID_IR_TX,
                               .payload = payload,
                               .length = sizeof(payload),
                               .out = response,
                               .capacity = sizeof(response)};
  return execute(&request, TOS_CAP_RADIO_TX);
}

#define IR_PULSE_MAX_US      32767
#define IR_FRAME_MAX_US      900000
#define IR_CARRIER_MIN_HZ    10000
#define IR_CARRIER_MAX_HZ    100000
#define IR_PATH_MAX          192
#define IR_PULSES_PER_SYMBOL 2
#define IR_SD_PREFIX         "/sdcard/"

typedef enum {
  TOS_IR_RAW_SEND,
  TOS_IR_RAW_GET,
  TOS_IR_FILE_SEND,
  TOS_IR_AC_SEND,
  TOS_IR_OPERATION_COUNT,
} tos_ir_operation_t;

typedef struct {
  tos_ir_operation_t operation;
  const int32_t *pulses;
  int32_t *out_pulses;
  int count;
  uint32_t carrier_hz;
  const char *path;
  tos_ir_ac_t ac;
  int copied;
} tos_ir_request_t;

// The persistent RF worker retains this storage until RMT teardown succeeds.
static rmt_symbol_word_t *s_symbols;

static esp_err_t cleanup_symbols(void) {
  ir_rx_deinit();
  ir_tx_deinit();
  esp_err_t error = ir_lease_release();
  if (error == ESP_OK) {
    free(s_symbols);
    s_symbols = NULL;
  }
  return error;
}

static esp_err_t allocate_symbols(size_t count) {
  s_symbols = calloc(count, sizeof(*s_symbols));
  if (s_symbols == NULL) {
    ESP_LOGE(TAG, "Cannot allocate IR symbol storage");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

static bool valid_carrier(uint32_t carrier) {
  return carrier == 0 || (carrier >= IR_CARRIER_MIN_HZ && carrier <= IR_CARRIER_MAX_HZ);
}

static esp_err_t encode_pulses(const int32_t *pulses, int count) {
  uint32_t total = 0;
  for (int i = 0; i < count; ++i) {
    int32_t pulse = pulses[i];
    if (pulse == 0 || pulse < -IR_PULSE_MAX_US || pulse > IR_PULSE_MAX_US)
      return ESP_ERR_INVALID_ARG;
    uint32_t duration = pulse < 0 ? (uint32_t)-pulse : (uint32_t)pulse;
    if (duration > IR_FRAME_MAX_US - total)
      return ESP_ERR_INVALID_SIZE;
    total += duration;
    rmt_symbol_word_t *symbol = &s_symbols[i / IR_PULSES_PER_SYMBOL];
    if (i % IR_PULSES_PER_SYMBOL == 0) {
      symbol->duration0 = duration;
      symbol->level0 = pulse > 0;
    } else {
      symbol->duration1 = duration;
      symbol->level1 = pulse > 0;
    }
  }
  return ESP_OK;
}

static bool valid_path(const char *path) {
  if (path == NULL || strlen(path) >= IR_PATH_MAX ||
      strncmp(path, IR_SD_PREFIX, sizeof(IR_SD_PREFIX) - 1) != 0)
    return false;
  const char *part = path + sizeof(IR_SD_PREFIX) - 1;
  while (*part != '\0') {
    const char *end = strchr(part, '/');
    size_t length = end == NULL ? strlen(part) : (size_t)(end - part);
    if (length == 0 || (length == 1 && part[0] == '.') ||
        (length == 2 && part[0] == '.' && part[1] == '.') || strchr(part, '\\') != NULL)
      return false;
    if (end == NULL)
      return true;
    part = end + 1;
  }
  return false;
}

static esp_err_t send_file_native(const char *path) {
  FILE *file = NULL;
  char *content = NULL;
  ir_file_t parsed;
  ir_file_init(&parsed);
  esp_err_t error = ESP_FAIL;
  file = fopen(path, "rb");
  if (file == NULL) {
    error = ESP_ERR_NOT_FOUND;
    goto cleanup;
  }
  if (fseek(file, 0, SEEK_END) != 0)
    goto cleanup;
  long size = ftell(file);
  if (size <= 0 || size > IR_FILE_CONTENT_MAX) {
    error = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }
  if (fseek(file, 0, SEEK_SET) != 0)
    goto cleanup;
  content = malloc((size_t)size + 1);
  if (content == NULL) {
    ESP_LOGE(TAG, "Cannot allocate IR file storage");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  if (fread(content, 1, size, file) != (size_t)size || ferror(file))
    goto cleanup;
  if (memchr(content, '\0', size) != NULL) {
    error = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }
  content[size] = '\0';
  error = ir_file_parse(content, &parsed);
  if (error != ESP_OK)
    goto cleanup;
  if (parsed.count != 1) {
    error = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }
  ir_signal_t *signal = &parsed.signals[0];
  if (!signal->is_raw) {
    size_t count = ir_encode(&signal->data, s_symbols, IR_MAX_SYMBOLS);
    error = count == 0 ? ESP_ERR_INVALID_ARG
                       : ir_send_raw(s_symbols, count, ir_carrier_freq(signal->data.protocol));
    goto cleanup;
  }
  if (signal->raw_count == 0 || signal->raw_count > IR_FILE_RAW_MAX_PULSES ||
      !valid_carrier(signal->frequency) || signal->frequency == 0) {
    error = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }
  uint32_t total = 0;
  for (size_t i = 0; i < signal->raw_count; ++i) {
    uint32_t duration = signal->raw[i];
    if (duration == 0 || duration > IR_PULSE_MAX_US || duration > IR_FRAME_MAX_US - total) {
      error = ESP_ERR_INVALID_SIZE;
      goto cleanup;
    }
    total += duration;
    rmt_symbol_word_t *symbol = &s_symbols[i / IR_PULSES_PER_SYMBOL];
    if (i % IR_PULSES_PER_SYMBOL == 0) {
      symbol->duration0 = duration;
      symbol->level0 = 1;
    } else {
      symbol->duration1 = duration;
      symbol->level1 = 0;
    }
  }
  error = ir_send_raw(s_symbols, (signal->raw_count + 1) / IR_PULSES_PER_SYMBOL, signal->frequency);
cleanup:
  free(content);
  ir_file_free(&parsed);
  if (file != NULL)
    fclose(file);
  return error;
}

static esp_err_t extended_worker(void *context) {
  tos_ir_request_t *request = context;
  esp_err_t cleanup_error;
  esp_err_t error = ir_lease_acquire();
  if (error != ESP_OK)
    return error;
  error = cleanup_symbols();
  if (error != ESP_OK)
    return error;
  error = ir_lease_acquire();
  if (error != ESP_OK)
    return error;
  size_t capacity = request->operation == TOS_IR_FILE_SEND
                        ? IR_FILE_RAW_MAX_PULSES / IR_PULSES_PER_SYMBOL
                        : IR_MAX_SYMBOLS;
  error = allocate_symbols(capacity);
  if (error != ESP_OK)
    goto cleanup;
  if (request->operation == TOS_IR_RAW_GET) {
    error = ir_rx_init();
    size_t count = 0;
    if (error == ESP_OK)
      error = ir_get_last_raw(s_symbols, IR_MAX_SYMBOLS, &count);
    if (error != ESP_OK)
      goto cleanup;
    for (size_t i = 0; i < count; ++i) {
      rmt_symbol_word_t symbol = s_symbols[i];
      uint32_t durations[] = {symbol.duration0, symbol.duration1};
      bool levels[] = {symbol.level0, symbol.level1};
      for (size_t j = 0; j < IR_PULSES_PER_SYMBOL; ++j) {
        if (durations[j] == 0)
          continue;
        if (request->copied == request->count) {
          error = ESP_ERR_INVALID_SIZE;
          goto cleanup;
        }
        request->out_pulses[request->copied++] =
            levels[j] ? (int32_t)durations[j] : -(int32_t)durations[j];
      }
    }
    goto cleanup;
  }
  error = ir_tx_init();
  if (error != ESP_OK)
    goto cleanup;
  if (request->operation == TOS_IR_RAW_SEND) {
    error = encode_pulses(request->pulses, request->count);
    if (error == ESP_OK)
      error =
          ir_send_raw(s_symbols, (request->count + 1) / IR_PULSES_PER_SYMBOL, request->carrier_hz);
  } else if (request->operation == TOS_IR_FILE_SEND) {
    error = send_file_native(request->path);
  } else {
    ir_ac_state_t state = {.protocol = request->ac.protocol + IR_AC_PROTO_COOLIX,
                           .power = request->ac.power,
                           .temp_c = request->ac.temp_c,
                           .fan = request->ac.fan};
    static const ir_ac_mode_t MODES[] = {
        IR_AC_MODE_AUTO, IR_AC_MODE_COOL, IR_AC_MODE_HEAT, IR_AC_MODE_DRY, IR_AC_MODE_FAN};
    state.mode = MODES[request->ac.mode];
    size_t count = ir_ac_encode(&state, s_symbols, capacity);
    error = count == 0 ? ESP_ERR_INVALID_ARG
                       : ir_send_raw(s_symbols, count, ir_ac_carrier_freq(state.protocol));
  }
cleanup:
  cleanup_error = cleanup_symbols();
  return cleanup_error == ESP_OK ? error : cleanup_error;
}

static esp_err_t extended_call(tos_ir_request_t *request, uint32_t caps) {
  if (!tos_app_cap_check(caps))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(IR_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t error = tos_api_get()->should_stop() ? ESP_ERR_INVALID_STATE
                                                 : host_local_rf_call(extended_worker, request);
  host_local_rf_unlock();
  return error;
}

static esp_err_t send_raw(const int32_t *pulses, int count, uint32_t carrier_hz) {
  if (pulses == NULL || count <= 0 || count > IR_MAX_SYMBOLS * IR_PULSES_PER_SYMBOL ||
      !valid_carrier(carrier_hz))
    return ESP_ERR_INVALID_ARG;
  tos_ir_request_t request = {
      .operation = TOS_IR_RAW_SEND, .pulses = pulses, .count = count, .carrier_hz = carrier_hz};
  return extended_call(&request, TOS_CAP_RADIO_TX);
}

static int get_last_raw(int32_t *out, int max) {
  if (out == NULL || max <= 0 || max > IR_MAX_SYMBOLS * IR_PULSES_PER_SYMBOL)
    return -ESP_ERR_INVALID_ARG;
  tos_ir_request_t request = {.operation = TOS_IR_RAW_GET, .out_pulses = out, .count = max};
  esp_err_t error = extended_call(&request, TOS_CAP_RADIO_RX);
  return error == ESP_OK ? request.copied : -error;
}

static esp_err_t send_file(const char *path) {
  if (!valid_path(path))
    return ESP_ERR_INVALID_ARG;
  tos_ir_request_t request = {.operation = TOS_IR_FILE_SEND, .path = path};
  return extended_call(&request, TOS_CAP_RADIO_TX | TOS_CAP_FS_READ);
}

static esp_err_t send_ac(const tos_ir_ac_t *ac) {
  if (ac == NULL || ac->protocol > TOS_IR_AC_HAIER || ac->mode > TOS_IR_AC_MODE_FAN ||
      ac->fan > TOS_IR_AC_FAN_HIGH)
    return ESP_ERR_INVALID_ARG;
  tos_ir_request_t request = {.operation = TOS_IR_AC_SEND, .ac = *ac};
  return extended_call(&request, TOS_CAP_RADIO_TX);
}

const tos_ir_api_t tos_ir_api_impl = {.receive = receive_code,
                                      .send = send_code,
                                      .send_raw = send_raw,
                                      .get_last_raw = get_last_raw,
                                      .send_file = send_file,
                                      .send_ac = send_ac};
