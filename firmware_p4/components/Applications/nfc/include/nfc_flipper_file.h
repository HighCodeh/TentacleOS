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

#ifndef NFC_FLIPPER_FILE_H
#define NFC_FLIPPER_FILE_H

#include "esp_err.h"
#include "mf_classic_emu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Loads complete Classic v3/v4 files; clears output on failure. */
esp_err_t nfc_flipper_file_load_classic(const char *path, mfc_emu_card_data_t *out_card);

/* Writes a complete Classic v4 file without replacing an existing path. */
esp_err_t nfc_flipper_file_save_classic(const char *path, const mfc_emu_card_data_t *card);

#ifdef __cplusplus
}
#endif
#endif
