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
 * @file nfc_apdu.h
 * @brief ISO/IEC 7816-4 APDU helpers over ISO14443-4 (T=CL).
 */
#ifndef NFC_APDU_H
#define NFC_APDU_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

/* ---- APDU class byte ---- */
#define APDU_CLA_ISO7816 0x00U

/* ---- APDU instruction codes (ISO 7816-4) ---- */
#define APDU_INS_SELECT        0xA4U
#define APDU_INS_READ_BINARY   0xB0U
#define APDU_INS_UPDATE_BINARY 0xD6U
#define APDU_INS_GET_RESPONSE  0xC0U
#define APDU_INS_VERIFY        0x20U
#define APDU_INS_GET_DATA      0xCAU

/* ---- APDU status words ---- */
#define APDU_SW_OK             0x9000U
#define APDU_SW_MORE_DATA      0x6100U /**< SW1=61: more data available. */
#define APDU_SW_WRONG_LENGTH   0x6700U
#define APDU_SW_SECURITY       0x6982U
#define APDU_SW_CONDITIONS     0x6985U /**< Conditions of use not satisfied. */
#define APDU_SW_AUTH_FAILED    0x6300U
#define APDU_SW_WRONG_P1P2     0x6A86U
#define APDU_SW_FILE_NOT_FOUND 0x6A82U
#define APDU_SW_WRONG_OFFSET   0x6B00U
#define APDU_SW_WRONG_LE       0x6C00U /**< SW1=6C: wrong Le, SW2=exact Le. */
#define APDU_SW_INS_NOT_SUP    0x6D00U
#define APDU_SW_CLA_NOT_SUP    0x6E00U
#define APDU_SW_UNKNOWN        0x6F00U

/* ---- APDU status word masks ---- */
#define APDU_SW_CLASS_MASK 0xFF00U /**< Mask for SW1 byte. */
#define APDU_SW_DATA_MASK  0x00FFU /**< Mask for SW2 byte. */

/* ---- APDU protocol constants ---- */
#define APDU_HEADER_SIZE     5U    /**< CLA + INS + P1 + P2 + Lc/Le. */
#define APDU_LE_MAX          0xFFU /**< Maximum short-form Le value. */
#define APDU_MAX_GET_RESP    8     /**< Max GET RESPONSE chaining retries. */
#define APDU_FCI_TAG_DF_NAME 0x84U /**< FCI template: DF name tag. */

#define NFC_APDU_MAX_COMMAND       1024U /**< Largest command accepted by the workbench. */
#define NFC_APDU_DEFAULT_EXCHANGES 16U   /**< Default automatic exchange limit. */

/** ISO/IEC 7816-4 command APDU encoding case. */
typedef enum {
  NFC_APDU_CASE_INVALID = 0,
  NFC_APDU_CASE_1,
  NFC_APDU_CASE_2_SHORT,
  NFC_APDU_CASE_3_SHORT,
  NFC_APDU_CASE_4_SHORT,
  NFC_APDU_CASE_2_EXTENDED,
  NFC_APDU_CASE_3_EXTENDED,
  NFC_APDU_CASE_4_EXTENDED,
} nfc_apdu_case_t;

/** Safety classification used by interactive APDU clients. */
typedef enum {
  NFC_APDU_RISK_READ_ONLY = 0,
  NFC_APDU_RISK_AUTHENTICATION,
  NFC_APDU_RISK_MUTATING,
  NFC_APDU_RISK_PAYMENT,
  NFC_APDU_RISK_UNKNOWN,
} nfc_apdu_risk_t;

/** Parsed command APDU view. Pointers reference the original command buffer. */
typedef struct {
  uint8_t cla;
  uint8_t ins;
  uint8_t p1;
  uint8_t p2;
  nfc_apdu_case_t apdu_case;
  const uint8_t *data;
  size_t lc;
  bool le_present;
  uint32_t le; /**< 1..65536 when present (zero encoded value means maximum). */
  size_t le_offset;
  uint8_t le_size;
  bool extended;
} nfc_apdu_cmd_info_t;

/** Parameters for a generic command builder. */
typedef struct {
  uint8_t cla;
  uint8_t ins;
  uint8_t p1;
  uint8_t p2;
  const uint8_t *data;
  size_t lc;
  bool le_present;
  uint32_t le; /**< 1..65536; 256/65536 are encoded as zero. */
  bool force_extended;
} nfc_apdu_command_t;

/** Automatic response handling policy. */
typedef struct {
  bool auto_correct_le;   /**< Retry a 6Cxx response with the suggested Le. */
  bool auto_get_response; /**< Follow 61xx responses using GET RESPONSE. */
  bool accept_9f_more;    /**< Also treat legacy 9Fxx as more data. */
  uint8_t max_exchanges;  /**< Includes retries and GET RESPONSE commands. */
} nfc_apdu_options_t;

#define NFC_APDU_OPTIONS_DEFAULT()                 \
  ((nfc_apdu_options_t){                           \
      .auto_correct_le = true,                     \
      .auto_get_response = true,                   \
      .accept_9f_more = false,                     \
      .max_exchanges = NFC_APDU_DEFAULT_EXCHANGES, \
  })

/**
 * @brief APDU output buffer descriptor.
 *
 * Groups a destination buffer pointer and its capacity for APDU build helpers.
 */
typedef struct {
  uint8_t *buf; /**< Destination buffer. */
  size_t max;   /**< Buffer capacity in bytes. */
} nfc_apdu_buf_t;

/**
 * @brief Read-only data span for APDU payloads (AID, PIN, etc.).
 */
typedef struct {
  const uint8_t *ptr; /**< Data pointer. */
  size_t len;         /**< Data length in bytes. */
} nfc_apdu_data_t;

/**
 * @brief APDU channel descriptor for ISO14443-4 transceive operations.
 *
 * Groups transport-level parameters shared across all APDU exchange calls.
 */
typedef struct {
  hb_nfc_protocol_t proto; /**< HB_PROTO_ISO14443_4A or HB_PROTO_ISO14443_4B. */
  const void *ctx;         /**< Protocol context (nfc_iso_dep_data_t* or nfc_iso14443b_data_t*). */
  int timeout_ms;          /**< Timeout in milliseconds. */
} nfc_apdu_channel_t;

/**
 * @brief APDU response descriptor.
 *
 * Groups output buffer, length, and status word for APDU responses.
 */
typedef struct {
  uint8_t *buf;        /**< Response data buffer. */
  size_t max;          /**< Buffer capacity in bytes. */
  size_t len;          /**< Actual response data length (excluding SW). */
  uint16_t sw;         /**< ISO 7816-4 status word. */
  bool truncated;      /**< Response data did not fit in buf. */
  bool le_corrected;   /**< A 6Cxx retry was performed. */
  uint8_t exchanges;   /**< Number of physical APDU exchanges. */
  uint32_t elapsed_us; /**< Total exchange duration. */
} nfc_apdu_resp_t;

/**
 * @brief FCI (File Control Information) parsed data.
 */
typedef struct {
  uint8_t df_name[16];
  uint8_t df_name_len;
} nfc_fci_info_t;

/**
 * @brief Translate a status word to a human-readable string.
 *
 * @param sw  ISO 7816-4 status word.
 * @return Pointer to a static string describing the status word.
 */
const char *nfc_apdu_sw_str(uint16_t sw);

/** Format a detailed ISO 7816 status description into a caller buffer. */
void nfc_apdu_sw_format(uint16_t sw, char *out, size_t out_size);

/** Return a short instruction name for common ISO 7816 commands. */
const char *nfc_apdu_ins_name(uint8_t cla, uint8_t ins);

/** Classify an APDU before interactive transmission. */
nfc_apdu_risk_t nfc_apdu_classify(const nfc_apdu_cmd_info_t *cmd);

/** Return a stable display name for a safety classification. */
const char *nfc_apdu_risk_name(nfc_apdu_risk_t risk);

/** Parse and strictly validate a short or extended command APDU. */
bool nfc_apdu_parse_command(const uint8_t *apdu, size_t apdu_len, nfc_apdu_cmd_info_t *out);

/** Build a short or extended command APDU. */
size_t nfc_apdu_build_command(nfc_apdu_buf_t dst, const nfc_apdu_command_t *cmd);

/** Decode hexadecimal text; spaces, colons, dashes and underscores are accepted. */
bool nfc_apdu_hex_decode(
    const char *text, uint8_t *out, size_t out_max, size_t *out_len, size_t *error_pos);

/** Encode bytes as uppercase hexadecimal text with an optional separator. */
size_t nfc_apdu_hex_encode(
    const uint8_t *data, size_t data_len, char separator, char *out, size_t out_size);

/**
 * @brief Map a status word to a coarse NFC error code.
 *
 * @param sw  ISO 7816-4 status word.
 * @return
 *   - HB_NFC_OK if sw is APDU_SW_OK
 *   - HB_NFC_ERR_AUTH on authentication/security failures
 *   - HB_NFC_ERR on other error status words
 */
hb_nfc_err_t nfc_apdu_sw_to_err(uint16_t sw);

/**
 * @brief Transceive a generic APDU over ISO14443-4A or 4B.
 *
 * @param ch         Channel descriptor (protocol, context, timeout).
 * @param apdu       Command APDU bytes.
 * @param apdu_len   Length of the command APDU.
 * @param[out] resp  Response descriptor (buffer, length, status word).
 * @return
 *   - HB_NFC_OK on SW=0x9000
 *   - Mapped error code otherwise
 */
hb_nfc_err_t nfc_apdu_transceive(const nfc_apdu_channel_t *ch,
                                 const uint8_t *apdu,
                                 size_t apdu_len,
                                 nfc_apdu_resp_t *resp);

/** Transceive using an explicit automatic response handling policy. */
hb_nfc_err_t nfc_apdu_transceive_ex(const nfc_apdu_channel_t *ch,
                                    const uint8_t *apdu,
                                    size_t apdu_len,
                                    const nfc_apdu_options_t *options,
                                    nfc_apdu_resp_t *resp);

/**
 * @brief Send an APDU command and receive the response.
 *
 * Convenience wrapper around nfc_apdu_transceive.
 *
 * @param ch         Channel descriptor (protocol, context, timeout).
 * @param apdu       Command APDU bytes.
 * @param apdu_len   Length of the command APDU.
 * @param[out] resp  Response descriptor (buffer, length, status word).
 * @return
 *   - HB_NFC_OK on success
 *   - Mapped error code on failure
 */
hb_nfc_err_t nfc_apdu_send(const nfc_apdu_channel_t *ch,
                           const uint8_t *apdu,
                           size_t apdu_len,
                           nfc_apdu_resp_t *resp);

/**
 * @brief Receive an APDU response for a previously sent command.
 *
 * Convenience wrapper around nfc_apdu_transceive.
 *
 * @param ch         Channel descriptor (protocol, context, timeout).
 * @param apdu       Command APDU bytes.
 * @param apdu_len   Length of the command APDU.
 * @param[out] resp  Response descriptor (buffer, length, status word).
 * @return
 *   - HB_NFC_OK on success
 *   - Mapped error code on failure
 */
hb_nfc_err_t nfc_apdu_recv(const nfc_apdu_channel_t *ch,
                           const uint8_t *apdu,
                           size_t apdu_len,
                           nfc_apdu_resp_t *resp);

/**
 * @brief Build a SELECT AID APDU.
 *
 * @param dst        Output buffer descriptor.
 * @param aid        Application identifier data span.
 * @param le_present Whether to include the Le byte.
 * @param le         Expected response length byte.
 * @return Number of bytes written to the output buffer.
 */
size_t
nfc_apdu_build_select_aid(nfc_apdu_buf_t dst, nfc_apdu_data_t aid, bool le_present, uint8_t le);

/**
 * @brief Build a SELECT FILE by file ID APDU.
 *
 * @param dst        Output buffer descriptor.
 * @param fid        File identifier.
 * @param le_present Whether to include the Le byte.
 * @param le         Expected response length byte.
 * @return Number of bytes written to the output buffer.
 */
size_t nfc_apdu_build_select_file(nfc_apdu_buf_t dst, uint16_t fid, bool le_present, uint8_t le);

/**
 * @brief Build a READ BINARY APDU.
 *
 * @param dst     Output buffer descriptor.
 * @param offset  Byte offset to read from.
 * @param le      Expected response length byte.
 * @return Number of bytes written to the output buffer.
 */
size_t nfc_apdu_build_read_binary(nfc_apdu_buf_t dst, uint16_t offset, uint8_t le);

/**
 * @brief Build an UPDATE BINARY APDU.
 *
 * @param dst        Output buffer descriptor.
 * @param offset     Byte offset to write to.
 * @param data       Data span to write.
 * @return Number of bytes written to the output buffer.
 */
size_t nfc_apdu_build_update_binary(nfc_apdu_buf_t dst, uint16_t offset, nfc_apdu_data_t data);

/**
 * @brief Build a VERIFY APDU for PIN or password authentication.
 *
 * @param dst        Output buffer descriptor.
 * @param p1         P1 parameter.
 * @param p2         P2 parameter (key reference).
 * @param data       Verification data span (PIN/password).
 * @return Number of bytes written to the output buffer.
 */
size_t nfc_apdu_build_verify(nfc_apdu_buf_t dst, uint8_t p1, uint8_t p2, nfc_apdu_data_t data);

/**
 * @brief Build a GET DATA APDU.
 *
 * @param dst   Output buffer descriptor.
 * @param p1    P1 tag identifier.
 * @param p2    P2 tag identifier.
 * @param le    Expected response length byte.
 * @return Number of bytes written to the output buffer.
 */
size_t nfc_apdu_build_get_data(nfc_apdu_buf_t dst, uint8_t p1, uint8_t p2, uint8_t le);

/**
 * @brief Parse an FCI template and extract the DF name (AID) if present.
 *
 * @param fci      FCI data bytes.
 * @param fci_len  Length of the FCI data.
 * @param[out] out     Parsed FCI information.
 * @return true if the FCI was parsed successfully, false otherwise.
 */
bool nfc_apdu_parse_fci(const uint8_t *fci, size_t fci_len, nfc_fci_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NFC_APDU_H */
