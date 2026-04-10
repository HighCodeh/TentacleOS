// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/**
 * @file mf_ultralight.h
 * @brief MIFARE Ultralight / NTAG READ, WRITE, PWD_AUTH, GET_VERSION.
 */
#ifndef MF_ULTRALIGHT_H
#define MF_ULTRALIGHT_H

#include <stdint.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read 4 pages (16 bytes) starting at the given page (cmd 0x30).
 *
 * @param page     Starting page number.
 * @param[out] out Buffer for 18 bytes (16 data + 2 CRC).
 * @return Number of bytes received (18 expected), or 0 on failure.
 */
int mful_read_pages(uint8_t page, uint8_t out[18]);

/**
 * @brief Write 4 bytes to one page (cmd 0xA2).
 *
 * @param page  Page number to write.
 * @param data  4 bytes to write.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mful_write_page(uint8_t page, const uint8_t data[4]);

/**
 * @brief Get version information (cmd 0x60, NTAG only).
 *
 * @param[out] out  Buffer for 8 bytes of version data.
 * @return Number of bytes received (8 expected), or 0 on failure.
 */
int mful_get_version(uint8_t out[8]);

/**
 * @brief Authenticate with a 4-byte password (cmd 0x1B).
 *
 * @param pwd       4-byte password.
 * @param[out] pack 2-byte PACK on success.
 * @return Number of bytes received (2 expected), or 0 on failure.
 */
int mful_pwd_auth(const uint8_t pwd[4], uint8_t pack[2]);

/**
 * @brief Perform MIFARE Ultralight C 3DES mutual authentication (cmd 0x1A).
 *
 * @param key  16-byte (2-key) 3DES key.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mful_ulc_auth(const uint8_t key[16]);

/**
 * @brief Read all pages of an Ultralight/NTAG card.
 *
 * @param[out] data  Output buffer (must be large enough for max_pages * 4 bytes).
 * @param max_pages  Maximum number of pages to read.
 * @return Number of pages read.
 */
int mful_read_all(uint8_t *data, int max_pages);

#ifdef __cplusplus
}
#endif

#endif /* MF_ULTRALIGHT_H */
