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

#ifndef TEXKOM_H
#define TEXKOM_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEXKOM_FRAME_SIZE 8U
#define TEXKOM_MAX_GAPS   127U
#define TEXKOM_TK17_GAPS  64U

typedef enum {
  TEXKOM_TYPE_TK13,
  TEXKOM_TYPE_TK15,
  TEXKOM_TYPE_TK17,
  TEXKOM_TYPE_MMBIT,
  TEXKOM_TYPE_COUNT,
} texkom_type_t;

typedef struct {
  uint8_t frame[TEXKOM_FRAME_SIZE];
  texkom_type_t type;
} texkom_data_t;

esp_err_t texkom_decode(const uint16_t *gaps, size_t count, texkom_data_t *out_data);
esp_err_t texkom_read(texkom_data_t *out_data);
/* out_count is capacity on input (at least 128), pulse count on success.
 * out_cycles contains low intervals in 13.56 MHz carrier cycles; highs are 64. */
esp_err_t texkom_encode(const texkom_data_t *data, uint16_t *out_cycles, size_t *out_count);
esp_err_t texkom_emulate_frame(const texkom_data_t *data);
const char *texkom_type_name(texkom_type_t type);

#ifdef __cplusplus
}
#endif
#endif
