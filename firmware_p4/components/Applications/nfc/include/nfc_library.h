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

#ifndef NFC_LIBRARY_H
#define NFC_LIBRARY_H

#include "nfc_archive.h"
#include "nfc_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Resolve a stored file's full UID and family; no RF access. */
esp_err_t nfc_library_file_info(const char *filename, nfc_store_info_t *out_info);
/** Delete all matching SD dumps/companions and vault aliases; requires mounted SD. */
esp_err_t nfc_library_delete_card(const nfc_store_info_t *card);
/** Delete the selected file and its linked card data; malformed files are file-only. */
esp_err_t nfc_library_delete_file(const char *filename, bool *out_card_deleted);
/** Rename a file and its companions, updating its unambiguous vault name. */
esp_err_t nfc_library_rename_file(const char *filename, const char *stem);

#ifdef __cplusplus
}
#endif

#endif
