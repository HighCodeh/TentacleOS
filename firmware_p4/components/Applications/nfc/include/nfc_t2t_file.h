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

#ifndef NFC_T2T_FILE_H
#define NFC_T2T_FILE_H

#include <stdint.h>
#include "esp_err.h"
#include "t2t_emu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_T2T_FILE_MAX_BYTES 1968U

typedef struct {
  t2t_emu_config_t config;
  uint8_t memory[NFC_T2T_FILE_MAX_BYTES];
  uint16_t length;
} nfc_t2t_file_t;

esp_err_t nfc_t2t_file_save(const char *path, const nfc_t2t_file_t *card);

esp_err_t nfc_t2t_file_load(const char *path, nfc_t2t_file_t *out_card);

#ifdef __cplusplus
}
#endif
#endif
