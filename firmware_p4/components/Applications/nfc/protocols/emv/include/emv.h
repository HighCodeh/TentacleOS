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
 * @file emv.h
 * @brief EMV contactless helpers (PPSE, AID select, GPO, READ RECORD).
 */
#ifndef EMV_H
#define EMV_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "highboy_nfc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EMV_AID_MAX_LEN   16U
#define EMV_APP_LABEL_MAX 16U

/**
 * @brief EMV application descriptor (AID + label).
 */
typedef struct {
  uint8_t aid[EMV_AID_MAX_LEN];       /**< Application Identifier bytes. */
  uint8_t aid_len;                    /**< Length of AID in bytes. */
  char label[EMV_APP_LABEL_MAX + 1U]; /**< Null-terminated application label. */
} emv_app_t;

/**
 * @brief Single entry from the Application File Locator (AFL).
 */
typedef struct {
  uint8_t sfi;                  /**< Short File Identifier. */
  uint8_t record_first;         /**< First record number. */
  uint8_t record_last;          /**< Last record number. */
  uint8_t offline_auth_records; /**< Number of records for offline auth. */
} emv_afl_entry_t;

#define EMV_FIELD_BYTES 27U
#define EMV_PDOL_BYTES  247U

typedef enum {
  EMV_FIELD_PAN,
  EMV_FIELD_AIP,
  EMV_FIELD_NAME,
  EMV_FIELD_LABEL,
  EMV_FIELD_HOLDER,
  EMV_FIELD_EXPIRY,
  EMV_FIELD_EFFECTIVE,
  EMV_FIELD_COUNTRY,
  EMV_FIELD_CURRENCY,
  EMV_FIELD_PIN_TRIES,
  EMV_FIELD_ATC,
  EMV_FIELD_LAST_ATC,
  EMV_FIELD_PRIORITY,
  EMV_FIELD_LOG_ENTRY,
  EMV_FIELD_COUNT
} emv_field_t;

#define EMV_LOG_COUNT        16U
#define EMV_LOG_FIELDS       6U
#define EMV_LOG_FORMAT_BYTES 50U
typedef struct {
  uint8_t value[EMV_LOG_FIELDS][6];
  uint8_t length[EMV_LOG_FIELDS];
} emv_log_t;

typedef struct {
  emv_app_t app;
  uint8_t value[EMV_FIELD_COUNT][EMV_FIELD_BYTES];
  uint8_t length[EMV_FIELD_COUNT];
  uint16_t records_read;
  uint16_t status;
  bool is_complete;
  uint8_t log_count;
  emv_log_t logs[EMV_LOG_COUNT];
} emv_profile_t;

hb_nfc_err_t emv_decode_metadata(const uint8_t *data, size_t length, emv_profile_t *profile);
hb_nfc_err_t emv_prepare_pdol(
    const uint8_t *dol, size_t length, uint8_t *out, size_t capacity, size_t *out_length);
hb_nfc_err_t emv_read_application(hb_nfc_protocol_t proto,
                                  const void *ctx,
                                  emv_profile_t *profile,
                                  bool (*should_cancel)(void),
                                  int timeout_ms);

/**
 * @brief Callback invoked for each EMV record read.
 *
 * @param sfi     Short File Identifier of the record.
 * @param record  Record number.
 * @param data    Record data bytes.
 * @param len     Length of record data.
 * @param user    User-provided context pointer.
 */
typedef void (*emv_record_cb_t)(
    uint8_t sfi, uint8_t record, const uint8_t *data, size_t len, void *user);

/**
 * @brief Select PPSE (2PAY.SYS.DDF01) and return FCI.
 *
 * @param proto       NFC protocol type (A or B).
 * @param ctx         Protocol-specific card context.
 * @param[out] fci    Output buffer for FCI template.
 * @param fci_max     FCI buffer capacity.
 * @param[out] fci_len  Set to actual FCI length on success.
 * @param[out] sw     Status word (SW1 << 8 | SW2).
 * @param timeout_ms  Timeout in milliseconds.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t emv_select_ppse(hb_nfc_protocol_t proto,
                             const void *ctx,
                             uint8_t *fci,
                             size_t fci_max,
                             size_t *fci_len,
                             uint16_t *sw,
                             int timeout_ms);

/**
 * @brief Extract AID entries from a PPSE FCI response.
 *
 * @param fci      FCI template bytes.
 * @param fci_len  Length of FCI data.
 * @param[out] out Output array of application descriptors.
 * @param max      Maximum entries in output array.
 * @return Number of AIDs extracted.
 */
size_t emv_extract_aids(const uint8_t *fci, size_t fci_len, emv_app_t *out, size_t max);

/**
 * @brief Select an application by AID and return FCI.
 *
 * @param proto       NFC protocol type.
 * @param ctx         Protocol-specific card context.
 * @param aid         Application Identifier bytes.
 * @param aid_len     Length of AID.
 * @param[out] fci    Output buffer for FCI template.
 * @param fci_max     FCI buffer capacity.
 * @param[out] fci_len  Set to actual FCI length on success.
 * @param[out] sw     Status word.
 * @param timeout_ms  Timeout in milliseconds.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t emv_select_aid(hb_nfc_protocol_t proto,
                            const void *ctx,
                            const uint8_t *aid,
                            size_t aid_len,
                            uint8_t *fci,
                            size_t fci_max,
                            size_t *fci_len,
                            uint16_t *sw,
                            int timeout_ms);

/**
 * @brief Send GET PROCESSING OPTIONS and return response.
 *
 * @param proto       NFC protocol type.
 * @param ctx         Protocol-specific card context.
 * @param pdol        PDOL data bytes (NULL if none).
 * @param pdol_len    Length of PDOL data.
 * @param[out] gpo    Output buffer for GPO response.
 * @param gpo_max     GPO buffer capacity.
 * @param[out] gpo_len  Set to actual GPO length on success.
 * @param[out] sw     Status word.
 * @param timeout_ms  Timeout in milliseconds.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t emv_get_processing_options(hb_nfc_protocol_t proto,
                                        const void *ctx,
                                        const uint8_t *pdol,
                                        size_t pdol_len,
                                        uint8_t *gpo,
                                        size_t gpo_max,
                                        size_t *gpo_len,
                                        uint16_t *sw,
                                        int timeout_ms);

/**
 * @brief Parse AFL entries from a GPO response.
 *
 * @param gpo      GPO response bytes.
 * @param gpo_len  Length of GPO data.
 * @param[out] out Output array of AFL entries.
 * @param max      Maximum entries in output array.
 * @return Number of AFL entries parsed.
 */
size_t emv_parse_afl(const uint8_t *gpo, size_t gpo_len, emv_afl_entry_t *out, size_t max);

/**
 * @brief Read a single EMV record by SFI and record number.
 *
 * @param proto       NFC protocol type.
 * @param ctx         Protocol-specific card context.
 * @param sfi         Short File Identifier.
 * @param record      Record number.
 * @param[out] out    Output buffer for record data.
 * @param out_max     Output buffer capacity.
 * @param[out] out_len  Set to actual record length on success.
 * @param[out] sw     Status word.
 * @param timeout_ms  Timeout in milliseconds.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t emv_read_record(hb_nfc_protocol_t proto,
                             const void *ctx,
                             uint8_t sfi,
                             uint8_t record,
                             uint8_t *out,
                             size_t out_max,
                             size_t *out_len,
                             uint16_t *sw,
                             int timeout_ms);

/**
 * @brief Read all records specified by an AFL, invoking a callback per record.
 *
 * @param proto       NFC protocol type.
 * @param ctx         Protocol-specific card context.
 * @param afl         Array of AFL entries.
 * @param afl_count   Number of AFL entries.
 * @param cb          Callback invoked for each record.
 * @param user        User context passed to callback.
 * @param timeout_ms  Timeout in milliseconds per record.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t emv_read_records(hb_nfc_protocol_t proto,
                              const void *ctx,
                              const emv_afl_entry_t *afl,
                              size_t afl_count,
                              emv_record_cb_t cb,
                              void *user,
                              int timeout_ms);

/**
 * @brief Run a basic EMV transaction: PPSE -> AID select -> GPO -> read records.
 *
 * @param proto       NFC protocol type.
 * @param ctx         Protocol-specific card context.
 * @param[out] app_out  First selected application descriptor.
 * @param cb          Callback invoked for each record read.
 * @param user        User context passed to callback.
 * @param timeout_ms  Timeout in milliseconds per APDU.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t emv_run_basic(hb_nfc_protocol_t proto,
                           const void *ctx,
                           emv_app_t *app_out,
                           emv_record_cb_t cb,
                           void *user,
                           int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* EMV_H */
