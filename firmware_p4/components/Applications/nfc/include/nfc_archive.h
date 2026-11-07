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

#ifndef NFC_ARCHIVE_H
#define NFC_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_ARCHIVE_PATH_SIZE 192U
#define NFC_ARCHIVE_MAX_BYTES (128U * 1024U)
#define NFC_ARCHIVE_DIRECTORY "/sdcard/nfc/dumps"

typedef struct {
  char *data;
  size_t length;
  size_t fields;
  char device[48];
  char uid[40];
  bool is_capture;
} nfc_archive_t;

typedef struct {
  const char *name;
  const uint8_t *data;
  size_t length;
} nfc_archive_field_t;

/* Validates the document envelope, not card permissions or emulation suitability. */
esp_err_t nfc_archive_load(const char *path, nfc_archive_t *out_archive);
void nfc_archive_free(nfc_archive_t *archive);
/* Creates a new file; source bytes (including unknown fields) remain identical. */
esp_err_t nfc_archive_export(const nfc_archive_t *archive, char *out_path, size_t capacity);
/* Captured fields only: no implicit claim that all card memory was read. */
esp_err_t nfc_archive_capture(const char *device,
                              const uint8_t *uid,
                              size_t uid_length,
                              const nfc_archive_field_t *fields,
                              size_t count,
                              char *out_path,
                              size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
