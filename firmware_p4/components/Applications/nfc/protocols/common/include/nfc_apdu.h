/**
 * @file nfc_apdu.h
 * @brief ISO/IEC 7816-4 APDU helpers over ISO14443-4 (T=CL).
 */
#ifndef NFC_APDU_H
#define NFC_APDU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

/* Common SW values */
#define APDU_SW_OK              0x9000U
#define APDU_SW_WRONG_LENGTH    0x6700U
#define APDU_SW_SECURITY        0x6982U
#define APDU_SW_AUTH_FAILED     0x6300U
#define APDU_SW_FILE_NOT_FOUND  0x6A82U
#define APDU_SW_INS_NOT_SUP     0x6D00U
#define APDU_SW_CLA_NOT_SUP     0x6E00U
#define APDU_SW_UNKNOWN         0x6F00U

typedef struct {
    uint8_t df_name[16];
    uint8_t df_name_len;
} nfc_fci_info_t;

/** Translate SW to a human-readable string. */
const char* nfc_apdu_sw_str(uint16_t sw);

/** Map SW to a coarse error code. */
hb_nfc_err_t nfc_apdu_sw_to_err(uint16_t sw);

/**
 * Generic APDU transceive over ISO14443-4A or 4B.
 *
 * @param proto HB_PROTO_ISO14443_4A or HB_PROTO_ISO14443_4B.
 * @param ctx   For A: nfc_iso_dep_data_t*; for B: nfc_iso14443b_data_t*.
 * @return HB_NFC_OK on SW=0x9000, or mapped error otherwise.
 */
hb_nfc_err_t nfc_apdu_transceive(hb_nfc_protocol_t proto,
                                 const void*        ctx,
                                 const uint8_t*     apdu,
                                 size_t             apdu_len,
                                 uint8_t*           out,
                                 size_t             out_max,
                                 size_t*            out_len,
                                 uint16_t*          sw,
                                 int                timeout_ms);

/* Convenience wrappers (same behavior as nfc_apdu_transceive). */
hb_nfc_err_t nfc_apdu_send(hb_nfc_protocol_t proto,
                           const void*        ctx,
                           const uint8_t*     apdu,
                           size_t             apdu_len,
                           uint8_t*           out,
                           size_t             out_max,
                           size_t*            out_len,
                           uint16_t*          sw,
                           int                timeout_ms);

hb_nfc_err_t nfc_apdu_recv(hb_nfc_protocol_t proto,
                           const void*        ctx,
                           const uint8_t*     apdu,
                           size_t             apdu_len,
                           uint8_t*           out,
                           size_t             out_max,
                           size_t*            out_len,
                           uint16_t*          sw,
                           int                timeout_ms);

/* APDU builders (short APDUs). */
size_t nfc_apdu_build_select_aid(uint8_t* out, size_t max,
                                 const uint8_t* aid, size_t aid_len,
                                 bool le_present, uint8_t le);

size_t nfc_apdu_build_select_file(uint8_t* out, size_t max,
                                  uint16_t fid, bool le_present, uint8_t le);

size_t nfc_apdu_build_read_binary(uint8_t* out, size_t max,
                                  uint16_t offset, uint8_t le);

size_t nfc_apdu_build_update_binary(uint8_t* out, size_t max,
                                    uint16_t offset,
                                    const uint8_t* data, size_t data_len);

size_t nfc_apdu_build_verify(uint8_t* out, size_t max,
                             uint8_t p1, uint8_t p2,
                             const uint8_t* data, size_t data_len);

size_t nfc_apdu_build_get_data(uint8_t* out, size_t max,
                               uint8_t p1, uint8_t p2, uint8_t le);

/** Parse FCI template and extract DF name (AID) if present. */
bool nfc_apdu_parse_fci(const uint8_t* fci, size_t fci_len, nfc_fci_info_t* out);

#endif
