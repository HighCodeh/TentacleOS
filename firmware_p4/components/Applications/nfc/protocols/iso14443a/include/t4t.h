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
 * @file t4t.h
 * @brief ISO14443-4 / Type 4 Tag (T4T) NDEF reader helpers.
 */
#ifndef T4T_H
#define T4T_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "highboy_nfc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief T4T Capability Container data.
 */
typedef struct {
  uint8_t mapping;
  uint8_t nlen_size;
  bool legacy;
  uint16_t cc_len;      /**< @brief CC file length. */
  uint16_t mle;         /**< @brief Maximum Le (read size). */
  uint16_t mlc;         /**< @brief Maximum Lc (write size). */
  uint16_t ndef_fid;    /**< @brief NDEF file identifier. */
  uint32_t ndef_max;    /**< @brief Maximum NDEF file size. */
  uint8_t read_access;  /**< @brief Read access condition. */
  uint8_t write_access; /**< @brief Write access condition. */
} t4t_cc_t;

/**
 * @brief Read and parse Capability Container (CC) file.
 *
 * @param dep      ISO-DEP session data.
 * @param[out] cc  Parsed CC data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_PROTOCOL on invalid CC
 */
hb_nfc_err_t t4t_read_cc(const nfc_iso_dep_data_t *dep, t4t_cc_t *cc);

/**
 * @brief Read NDEF file using CC information.
 *
 * @param dep          ISO-DEP session data.
 * @param cc           Capability Container data.
 * @param[out] out     Buffer for NDEF data.
 * @param out_max      Maximum output buffer size.
 * @param[out] out_len Actual NDEF length read.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_NO_MEM if buffer is too small
 */
hb_nfc_err_t t4t_read_ndef(const nfc_iso_dep_data_t *dep,
                           const t4t_cc_t *cc,
                           uint8_t *out,
                           size_t out_max,
                           size_t *out_len);

/**
 * @brief Write NDEF file (updates NLEN and payload).
 *
 * @param dep       ISO-DEP session data.
 * @param cc        Capability Container data.
 * @param data      NDEF data to write.
 * @param data_len  Length of NDEF data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_PROTOCOL on write failure
 */
hb_nfc_err_t t4t_write_ndef(const nfc_iso_dep_data_t *dep,
                            const t4t_cc_t *cc,
                            const uint8_t *data,
                            size_t data_len);

/**
 * @brief Read binary from NDEF file (raw access, no NLEN handling).
 *
 * @param dep      ISO-DEP session data.
 * @param cc       Capability Container data.
 * @param offset   Byte offset to read from.
 * @param[out] out Buffer for read data.
 * @param out_len  Number of bytes to read.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_PROTOCOL on read failure
 */
hb_nfc_err_t t4t_read_binary_ndef(const nfc_iso_dep_data_t *dep,
                                  const t4t_cc_t *cc,
                                  uint32_t offset,
                                  uint8_t *out,
                                  size_t out_len);

/**
 * @brief Update binary in NDEF file (raw access, no NLEN handling).
 *
 * @param dep       ISO-DEP session data.
 * @param cc        Capability Container data.
 * @param offset    Byte offset to write to.
 * @param data      Data to write.
 * @param data_len  Number of bytes to write.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_PROTOCOL on write failure
 */
hb_nfc_err_t t4t_update_binary_ndef(const nfc_iso_dep_data_t *dep,
                                    const t4t_cc_t *cc,
                                    uint32_t offset,
                                    const uint8_t *data,
                                    size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* T4T_H */
