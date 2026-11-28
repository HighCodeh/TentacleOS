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

#ifndef FLIPPER_SUBGHZ_H
#define FLIPPER_SUBGHZ_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FLIPPER_SUBGHZ_FIELDS_SIZE 1024U
#define FLIPPER_SUBGHZ_KEY_SIZE    96U

typedef struct {
  const char *name;
  bool can_decode;
  bool can_encode;
  bool is_dynamic;
} flipper_subghz_protocol_info_t;

typedef struct {
  const char *name;
  uint16_t bits;
  uint64_t key;
  char key_text[FLIPPER_SUBGHZ_KEY_SIZE];
  char fields[FLIPPER_SUBGHZ_FIELDS_SIZE];
} flipper_subghz_result_t;

size_t flipper_subghz_count(void);
bool flipper_subghz_info(size_t index, flipper_subghz_protocol_info_t *out_info);
esp_err_t
flipper_subghz_decode(const int32_t *pulses, size_t count, flipper_subghz_result_t *out_result);
esp_err_t flipper_subghz_decode_named(const char *name,
                                      const int32_t *pulses,
                                      size_t count,
                                      flipper_subghz_result_t *out_result);
esp_err_t
flipper_subghz_encode(const char *fields, int32_t *pulses, size_t capacity, size_t *out_count);
/* Guarded upstream calls use one temporary PSRAM arena and never touch the radio. */
typedef esp_err_t (*flipper_subghz_job_cb_t)(void *context);
esp_err_t flipper_subghz_guard(flipper_subghz_job_cb_t job, void *context);
#ifdef __cplusplus
}
#endif
#endif
