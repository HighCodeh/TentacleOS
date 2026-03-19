/**
 * @file mf_ultralight.h
 * @brief MIFARE Ultralight / NTAG READ, WRITE, PWD_AUTH, GET_VERSION.
 *
 * These commands are proven in the working code (ntag_get_version,
 * ntag_pwd_auth, st25r_read_pages).
 */
#ifndef MF_ULTRALIGHT_H
#define MF_ULTRALIGHT_H

#include <stdint.h>
#include "highboy_nfc_error.h"

/**
 * READ (cmd 0x30) reads 4 pages (16 bytes) starting at page.
 * From working code st25r_read_pages().
 * Returns bytes received (16 expected + 2 CRC = 18), 0 on fail.
 */
int mful_read_pages(uint8_t page, uint8_t out[18]);

/**
 * WRITE (cmd 0xA2) writes 4 bytes to one page.
 */
hb_nfc_err_t mful_write_page(uint8_t page, const uint8_t data[4]);

/**
 * GET_VERSION (cmd 0x60) returns 8 bytes (NTAG).
 * From working code ntag_get_version().
 */
int mful_get_version(uint8_t out[8]);

/**
 * PWD_AUTH (cmd 0x1B) authenticate with 4-byte password.
 * Returns PACK (2 bytes) on success.
 * From working code ntag_pwd_auth().
 */
int mful_pwd_auth(const uint8_t pwd[4], uint8_t pack[2]);

/**
 * AUTH (cmd 0x1A) MIFARE Ultralight C 3DES mutual authentication.
 * @param key 16-byte (2-key) 3DES key.
 * @return HB_NFC_OK on success.
 */
hb_nfc_err_t mful_ulc_auth(const uint8_t key[16]);

/**
 * READ all pages of an Ultralight/NTAG card.
 * @param data Output buffer (must be large enough).
 * @param max_pages Maximum number of pages to read.
 * @return Number of pages read.
 */
int mful_read_all(uint8_t* data, int max_pages);

#endif
