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

#include "flipper_subghz.h"
#include "flipper_subghz_compat.h"
#include "lib/subghz/types.h"
#include "lib/subghz/protocols/base.h"
#include "lib/subghz/blocks/generic.h"
#include "lib/subghz/blocks/custom_btn.h"
#include "lib/subghz/environment.h"
#include "lib/subghz/protocols/bin_raw.h"

#ifndef KEYSTORE_SD
#define KEYSTORE_SD "/sdcard/config/subghz/keeloq_mfcodes.txt"
#endif
#define KEYSTORE_FLASH "/assets/config/subghz/keeloq_mfcodes.txt"
#define NICE_TABLE     "/sdcard/subghz/assets/nice_flor_s"
#define ALUTECH_TABLE  "/sdcard/subghz/assets/alutech_at_4n"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#define CODEC_YIELD_INTERVAL      4096U
#define MAX_INPUT_PULSES          262144U
#define MAX_OUTPUT_PULSES         262144U
#define KEY_BYTE_BITS             8U
#define HEX_RADIX                 16U
#define YIELD_WAIT_LIMIT          8U
#define OFFLINE_BURST_IDLE_RSSI   -120.0f
#define OFFLINE_BURST_ACTIVE_RSSI 0.0f

extern const SubGhzProtocol *const flipper_subghz_catalog[];
extern const size_t flipper_subghz_catalog_count;

typedef struct {
  const int32_t *pulses;
  size_t count;
  const char *name;
  flipper_subghz_result_t *result;
  FlipperFormat *format;
  FuriString *preset_name;
  bool found;
  bool too_large;
} flipper_subghz_decode_job_t;

typedef struct {
  const char *fields;
  int32_t *pulses;
  size_t capacity;
  size_t *count;
} flipper_subghz_encode_job_t;

static const SubGhzProtocol *find_protocol(const char *name) {
  for (size_t i = 0; i < flipper_subghz_catalog_count; i++) {
    if (strcmp(name, flipper_subghz_catalog[i]->name) == 0)
      return flipper_subghz_catalog[i];
  }
  return NULL;
}
size_t flipper_subghz_count(void) {
  return flipper_subghz_catalog_count;
}
bool flipper_subghz_info(size_t index, flipper_subghz_protocol_info_t *out_info) {
  if (index >= flipper_subghz_catalog_count || !out_info)
    return false;
  const SubGhzProtocol *p = flipper_subghz_catalog[index];
  *out_info = (flipper_subghz_protocol_info_t){
      .name = p->name,
      .can_decode = p->decoder && p->decoder->feed,
      .can_encode = (p->flag & SubGhzProtocolFlag_Send) && p->encoder && p->encoder->deserialize &&
                    p->encoder->yield,
      .is_dynamic = p->type == SubGhzProtocolTypeDynamic};
  return true;
}
static SubGhzEnvironment *environment(bool needs_keystore) {
  SubGhzEnvironment *env = subghz_environment_alloc();
  if (needs_keystore && !subghz_environment_load_keystore(env, KEYSTORE_SD))
    subghz_environment_load_keystore(env, KEYSTORE_FLASH);
  subghz_environment_set_nice_flor_s_rainbow_table_file_name(env, NICE_TABLE);
  subghz_environment_set_alutech_at_4n_rainbow_table_file_name(env, ALUTECH_TABLE);
  return env;
}
static void decoded(SubGhzProtocolDecoderBase *base, void *context) {
  flipper_subghz_decode_job_t *job = context;
  if (job->found)
    return;
  SubGhzRadioPreset preset = {.name = job->preset_name};
  stream_clean(job->format);
  if (!base->protocol->decoder->serialize ||
      base->protocol->decoder->serialize(base, job->format, &preset) != SubGhzProtocolStatusOk ||
      job->format->failed)
    return;
  const char *body = strstr(job->format->text, "\nProtocol: ");
  if (!body)
    return;
  ++body;
  if (strlen(body) >= sizeof(job->result->fields)) {
    job->too_large = true;
    return;
  }
  uint32_t bits = 0;
  flipper_format_rewind(job->format);
  if (!flipper_format_read_uint32(job->format, "Bit", &bits, 1) || bits > UINT16_MAX)
    return;
  FuriString *key = furi_string_alloc();
  flipper_format_rewind(job->format);
  if (!flipper_format_read_string(job->format, "Key", key)) {
    if (base->protocol->type != SubGhzProtocolTypeBinRAW)
      return;
    furi_string_set(key, "RAW");
  }
  if (strlen(key->text) >= sizeof(job->result->key_text)) {
    job->too_large = true;
    return;
  }
  job->result->name = base->protocol->name;
  job->result->bits = (uint16_t)bits;
  memcpy(job->result->fields, body, strlen(body) + 1);
  memcpy(job->result->key_text, key->text, strlen(key->text) + 1);
  if (bits <= sizeof(uint64_t) * KEY_BYTE_BITS) {
    const char *pos = key->text;
    uint64_t value = 0;
    while (*pos) {
      char *end;
      unsigned long byte = strtoul(pos, &end, HEX_RADIX);
      if (end == pos || byte > UINT8_MAX)
        break;
      value = (value << KEY_BYTE_BITS) | byte;
      pos = end;
      while (*pos == ' ')
        ++pos;
    }
    job->result->key = value;
  }
  job->found = true;
}
static esp_err_t decode_job(void *context) {
  flipper_subghz_decode_job_t *job = context;
  job->format = malloc(sizeof(*job->format));
  job->preset_name = furi_string_alloc_set("AM650");
  const SubGhzProtocol *selected = job->name != NULL ? find_protocol(job->name) : NULL;
  SubGhzEnvironment *env =
      environment(selected == NULL || selected->type == SubGhzProtocolTypeDynamic);
  size_t base_mark = flipper_subghz_mark();
  for (size_t index = 0; index < flipper_subghz_catalog_count; index++) {
    const SubGhzProtocol *p = flipper_subghz_catalog[index];
    if (job->name && strcmp(job->name, p->name) != 0)
      continue;
    if (!p->decoder || !p->decoder->alloc || !p->decoder->feed)
      continue;
    /* BinRAW classifies arbitrary pulses; native RAW owns automatic fallback. */
    if (p->type == SubGhzProtocolTypeBinRAW && !job->name)
      continue;
    flipper_subghz_rewind(base_mark);
    subghz_block_generic_global_reset(NULL);
    subghz_custom_btns_reset();
    void *decoder = p->decoder->alloc(env);
    if (!decoder)
      return ESP_ERR_NO_MEM;
    if (p->decoder->reset)
      p->decoder->reset(decoder);
    subghz_protocol_decoder_base_set_decoder_callback(decoder, decoded, job);
    if (p->type == SubGhzProtocolTypeBinRAW) {
      /* The supplied pulse buffer already has burst boundaries; no RF strength is inferred. */
      subghz_protocol_decoder_bin_raw_data_input_rssi(decoder, OFFLINE_BURST_IDLE_RSSI);
      subghz_protocol_decoder_bin_raw_data_input_rssi(decoder, OFFLINE_BURST_ACTIVE_RSSI);
    }
    for (size_t i = 0; i < job->count && !job->found; i++) {
#ifdef ESP_PLATFORM
      if (i != 0 && i % CODEC_YIELD_INTERVAL == 0)
        vTaskDelay(1);
#endif
      int64_t pulse = job->pulses[i];
      p->decoder->feed(decoder, pulse > 0, (uint32_t)(pulse > 0 ? pulse : -pulse));
    }
    if (p->type == SubGhzProtocolTypeBinRAW)
      subghz_protocol_decoder_bin_raw_data_input_rssi(decoder, OFFLINE_BURST_IDLE_RSSI);
    if (p->decoder->free)
      p->decoder->free(decoder);
    if (job->found)
      return ESP_OK;
  }
  return job->too_large ? ESP_ERR_INVALID_SIZE : ESP_ERR_NOT_FOUND;
}
esp_err_t flipper_subghz_decode_named(const char *name,
                                      const int32_t *pulses,
                                      size_t count,
                                      flipper_subghz_result_t *out_result) {
  if (!pulses || !out_result || count == 0 || count > MAX_INPUT_PULSES)
    return ESP_ERR_INVALID_ARG;
  memset(out_result, 0, sizeof(*out_result));
  for (size_t i = 0; i < count; i++)
    if (pulses[i] == 0 || pulses[i] == INT32_MIN)
      return ESP_ERR_INVALID_ARG;
  flipper_subghz_decode_job_t job = {
      .pulses = pulses, .count = count, .name = name, .result = out_result};
  esp_err_t error = flipper_subghz_guard(decode_job, &job);
  if (error != ESP_OK)
    memset(out_result, 0, sizeof(*out_result));
  return error;
}
esp_err_t
flipper_subghz_decode(const int32_t *pulses, size_t count, flipper_subghz_result_t *out_result) {
  return flipper_subghz_decode_named(NULL, pulses, count, out_result);
}
static bool validate_fields(FlipperFormat *format) {
  static const struct {
    const char *name;
    uint32_t maximum;
  } numbers[] = {{"Bit", UINT16_MAX},
                 {"Serial", UINT32_MAX},
                 {"Btn", UINT8_MAX},
                 {"TE", 10000U},
                 {"Guard_time", 1000U}};
  for (const char *line = format->text; *line != '\0';) {
    const char *end = strchr(line, '\n');
    const char *colon = strchr(line, ':');
    if (colon != NULL && (end == NULL || colon < end)) {
      size_t length = (size_t)(colon - line);
      bool repeatable = (length == strlen("Bit_RAW") && strncmp(line, "Bit_RAW", length) == 0) ||
                        (length == strlen("Data_RAW") && strncmp(line, "Data_RAW", length) == 0);
      if (!repeatable && end != NULL) {
        for (const char *next = end + 1; *next != '\0';) {
          if (strncmp(line, next, length + 1) == 0)
            return false;
          next = strchr(next, '\n');
          if (next == NULL)
            break;
          ++next;
        }
      }
    }
    if (end == NULL)
      break;
    line = end + 1;
  }
  for (size_t i = 0; i < COUNT_OF(numbers); ++i) {
    char field[32];
    snprintf(field, sizeof(field), "\n%s:", numbers[i].name);
    bool present = strstr(format->text, field) != NULL ||
                   (strncmp(format->text, field + 1, strlen(field) - 1) == 0);
    if (!present)
      continue;
    uint32_t value = 0;
    flipper_format_rewind(format);
    if (!flipper_format_read_uint32(format, numbers[i].name, &value, 1) ||
        value > numbers[i].maximum)
      return false;
    if (strcmp(numbers[i].name, "TE") == 0 && value == 0)
      return false;
  }
  flipper_format_rewind(format);
  return true;
}
static esp_err_t encode_job(void *context) {
  flipper_subghz_encode_job_t *job = context;
  FlipperFormat *format = malloc(sizeof(*format));
  size_t length = strlen(job->fields);
  if (length >= sizeof(format->text))
    return ESP_ERR_INVALID_SIZE;
  memcpy(format->text, job->fields, length + 1);
  if (!validate_fields(format))
    return ESP_ERR_INVALID_ARG;
  FuriString *name = furi_string_alloc();
  if (!flipper_format_read_string(format, "Protocol", name))
    return ESP_ERR_INVALID_ARG;
  const SubGhzProtocol *p = find_protocol(name->text);
  if (!p || !(p->flag & SubGhzProtocolFlag_Send) || !p->encoder || !p->encoder->alloc ||
      !p->encoder->deserialize || !p->encoder->yield)
    return ESP_ERR_NOT_SUPPORTED;
  uint32_t repeat = 1;
  if (!flipper_format_insert_or_update_uint32(format, "Repeat", &repeat, 1))
    return ESP_ERR_INVALID_SIZE;
  SubGhzEnvironment *env = environment(p->type == SubGhzProtocolTypeDynamic);
  subghz_block_generic_global_reset(NULL);
  subghz_custom_btns_reset();
  void *encoder = p->encoder->alloc(env);
  if (!encoder)
    return ESP_ERR_NO_MEM;
  flipper_format_rewind(format);
  if (p->encoder->deserialize(encoder, format) != SubGhzProtocolStatusOk)
    return ESP_ERR_INVALID_ARG;
  unsigned waits = 0;
  size_t count = 0;
  for (size_t i = 0; i < MAX_OUTPUT_PULSES; i++) {
    LevelDuration item = p->encoder->yield(encoder);
    if (level_duration_is_reset(item)) {
      if (p->encoder->stop)
        p->encoder->stop(encoder);
      if (p->encoder->free)
        p->encoder->free(encoder);
      *job->count = count;
      return count ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }
    if (level_duration_is_wait(item)) {
      if (++waits > YIELD_WAIT_LIMIT)
        return ESP_ERR_TIMEOUT;
      continue;
    }
    uint32_t duration = level_duration_get_duration(item);
    if (duration == 0 || duration > INT32_MAX)
      return ESP_ERR_INVALID_RESPONSE;
    int32_t pulse = level_duration_get_level(item) ? (int32_t)duration : -(int32_t)duration;
    if (count && (job->pulses[count - 1] > 0) == (pulse > 0)) {
      int64_t merged = (int64_t)job->pulses[count - 1] + pulse;
      if (merged > INT32_MAX || merged < -INT32_MAX)
        return ESP_ERR_INVALID_SIZE;
      job->pulses[count - 1] = (int32_t)merged;
    } else {
      if (count == job->capacity)
        return ESP_ERR_INVALID_SIZE;
      job->pulses[count++] = pulse;
    }
  }
  return ESP_ERR_INVALID_SIZE;
}
esp_err_t
flipper_subghz_encode(const char *fields, int32_t *pulses, size_t capacity, size_t *out_count) {
  if (!fields || !pulses || !out_count || !capacity)
    return ESP_ERR_INVALID_ARG;
  *out_count = 0;
  flipper_subghz_encode_job_t job = {
      .fields = fields, .pulses = pulses, .capacity = capacity, .count = out_count};
  esp_err_t error = flipper_subghz_guard(encode_job, &job);
  if (error != ESP_OK)
    *out_count = 0;
  return error;
}

typedef struct {
  const char *fields;
  char *out_text;
  size_t capacity;
} flipper_subghz_describe_job_t;

static esp_err_t describe_job(void *context) {
  flipper_subghz_describe_job_t *job = context;
  FlipperFormat *format = malloc(sizeof(*format));
  size_t length = strlen(job->fields);
  if (length >= sizeof(format->text))
    return ESP_ERR_INVALID_SIZE;
  memcpy(format->text, job->fields, length + 1);
  if (!validate_fields(format))
    return ESP_ERR_INVALID_ARG;
  FuriString *name = furi_string_alloc();
  if (!flipper_format_read_string(format, "Protocol", name))
    return ESP_ERR_INVALID_ARG;
  const SubGhzProtocol *protocol = find_protocol(name->text);
  if (!protocol || !protocol->decoder || !protocol->decoder->alloc ||
      !protocol->decoder->deserialize || !protocol->decoder->get_string)
    return ESP_ERR_NOT_SUPPORTED;
  subghz_block_generic_global_reset(NULL);
  subghz_custom_btns_reset();
  void *decoder =
      protocol->decoder->alloc(environment(protocol->type == SubGhzProtocolTypeDynamic));
  if (!decoder)
    return ESP_ERR_NO_MEM;
  flipper_format_rewind(format);
  if (protocol->decoder->deserialize(decoder, format) != SubGhzProtocolStatusOk)
    return ESP_ERR_INVALID_ARG;
  FuriString *description = furi_string_alloc();
  protocol->decoder->get_string(decoder, description);
  length = strlen(description->text);
  if (length >= job->capacity)
    return ESP_ERR_INVALID_SIZE;
  memcpy(job->out_text, description->text, length + 1);
  if (protocol->decoder->free)
    protocol->decoder->free(decoder);
  return ESP_OK;
}

esp_err_t flipper_subghz_describe(const char *fields, char *out_text, size_t capacity) {
  if (!fields || !out_text || !capacity)
    return ESP_ERR_INVALID_ARG;
  out_text[0] = '\0';
  flipper_subghz_describe_job_t job = {
      .fields = fields, .out_text = out_text, .capacity = capacity};
  esp_err_t error = flipper_subghz_guard(describe_job, &job);
  if (error != ESP_OK)
    out_text[0] = '\0';
  return error;
}
