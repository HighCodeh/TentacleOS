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

#ifndef SUBGHZ_RAW_FILE_H
#define SUBGHZ_RAW_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"
#include "cc1101.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct subghz_raw_file subghz_raw_file_t;
typedef struct {
  FILE *file;
  bool is_in_data;
  bool has_samples;
} subghz_raw_reader_t;

/* Temporary files are deleted on final release unless explicitly saved. */
esp_err_t subghz_raw_file_create(uint32_t frequency,
                                 const cc1101_radio_config_t *radio,
                                 subghz_raw_file_t **out_file);
esp_err_t subghz_raw_file_append(subghz_raw_file_t *file, const int32_t *pulses, size_t count);
esp_err_t subghz_raw_file_finish(subghz_raw_file_t *file);
esp_err_t subghz_raw_file_save(subghz_raw_file_t *file, const char *name);
esp_err_t subghz_raw_file_load(const char *name, subghz_raw_file_t **out_file);
void subghz_raw_file_acquire(subghz_raw_file_t *file);
void subghz_raw_file_release(subghz_raw_file_t *file);
void subghz_raw_file_info(const subghz_raw_file_t *file,
                          uint32_t *out_frequency,
                          cc1101_radio_config_t *out_radio,
                          uint64_t *out_count);
esp_err_t subghz_raw_reader_open(const subghz_raw_file_t *file, subghz_raw_reader_t *out_reader);
esp_err_t subghz_raw_reader_next(subghz_raw_reader_t *reader, int32_t *out_pulse, bool *out_eof);
esp_err_t subghz_raw_reader_rewind(subghz_raw_reader_t *reader);
void subghz_raw_reader_close(subghz_raw_reader_t *reader);

#ifdef __cplusplus
}
#endif
#endif
