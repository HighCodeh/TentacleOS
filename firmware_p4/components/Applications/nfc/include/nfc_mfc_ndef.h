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

#ifndef NFC_MFC_NDEF_H
#define NFC_MFC_NDEF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "highboy_nfc_compat.h"
#include "nfc_mfc_dump.h"

/** The on-screen keyboard currently accepts at most 512 UTF-8 bytes. */
#define NFC_MFC_NDEF_TEXT_MAX 512U

/**
 * Build a MAD1 + NFC Forum Text NDEF data image for a MIFARE Classic 1K.
 *
 * Only blocks selected in @p write_mask belong to the generated application.
 * Block 0 and every sector trailer are always left unselected. The caller can
 * therefore write the image without changing UID, keys, access bits or GPB.
 */
hb_nfc_err_t nfc_mfc_ndef_build_text(const char *text,
                                     uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE],
                                     bool write_mask[NFC_MFC1K_BLOCKS],
                                     int *blocks_required,
                                     size_t *ndef_len);

/** Return the number of data blocks needed for a UTF-8 Text NDEF message. */
int nfc_mfc_ndef_text_blocks_required(const char *text);

/**
 * Validate MAD1, collect its NFC sectors and decode the first UTF-8 Text
 * record from the mandatory NDEF Message TLV.
 */
hb_nfc_err_t nfc_mfc_ndef_extract_text(const uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE],
                                       const bool block_read[NFC_MFC1K_BLOCKS],
                                       char *out,
                                       size_t out_max,
                                       size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // NFC_MFC_NDEF_H
