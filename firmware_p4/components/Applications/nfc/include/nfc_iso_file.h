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

#ifndef NFC_ISO_FILE_H
#define NFC_ISO_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "t4t_emu.h"

#ifdef __cplusplus
extern "C" {
#endif

bool nfc_iso_file_hex(const char *text, uint8_t *out, size_t capacity, size_t *out_size);
bool nfc_iso_file_valid_identity(const t4t_emu_identity_t *identity);
bool nfc_iso_file_parse_identity(char *const *values, t4t_emu_identity_t *identity);
bool nfc_iso_file_emit(
    char **cursor, size_t *remaining, const char *key, const uint8_t *data, size_t size);
bool nfc_iso_file_begin(char **cursor,
                        size_t *remaining,
                        const char *device,
                        const t4t_emu_identity_t *identity);

#ifdef __cplusplus
}
#endif
#endif
