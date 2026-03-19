/**
 * @file iso14443a.h
 * @brief ISO14443A types and CRC_A.
 */
#ifndef ISO14443A_H
#define ISO14443A_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_types.h"

/** Calculate CRC_A. Initial value 0x6363. */
void iso14443a_crc(const uint8_t* data, size_t len, uint8_t crc[2]);

/** Verify CRC_A on received data (last 2 bytes are CRC). */
bool iso14443a_check_crc(const uint8_t* data, size_t len);

#endif
