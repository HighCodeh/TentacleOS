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
/**
 * @file mf_ultralight.h
 * @brief MIFARE Ultralight / NTAG READ, WRITE, PWD_AUTH, GET_VERSION.
 */
#ifndef MF_ULTRALIGHT_H
#define MF_ULTRALIGHT_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_error.h"
#include "t2t_aes.h"
#include "nfc_t2t_file.h"
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
/* Key uses AES byte order; secure must match the card SEC_MSG_ACT setting. */
esp_err_t mful_aes_write_page(t2t_aes_session_t *session, uint8_t page, const uint8_t data[4]);

esp_err_t
mful_aes_auth(t2t_aes_session_t *session, const uint8_t *key, uint8_t key_number, bool secure);
esp_err_t
mful_aes_read_card(t2t_aes_session_t *session, const uint8_t version[8], nfc_t2t_file_t *out_card);
int mful_aes_read_all(t2t_aes_session_t *session, uint8_t *out_data, size_t capacity);
bool mful_is_aes(const uint8_t version[8]);
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

/* Returns the page count for a recognized single-sector memory map, or zero. */
uint16_t mful_version_pages(const uint8_t version[8]);
uint16_t mful_i2c_pages(const uint8_t version[8]);
/* Flipper linear page order, including configuration/session register regions. */
int mful_read_i2c(const uint8_t version[8], uint8_t *out_data, size_t capacity);

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
