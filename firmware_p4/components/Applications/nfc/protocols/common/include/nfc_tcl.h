/**
 * @file nfc_tcl.h
 * @brief ISO14443-4 (T=CL) transport wrapper for A/B.
 */
#ifndef NFC_TCL_H
#define NFC_TCL_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

/**
 * Generic ISO14443-4 transceive.
 *
 * @param proto   HB_PROTO_ISO14443_4A or HB_PROTO_ISO14443_4B.
 * @param ctx     For A: pointer to nfc_iso_dep_data_t
 *                For B: pointer to nfc_iso14443b_data_t
 * @return number of bytes received, 0 on failure.
 */
int nfc_tcl_transceive(hb_nfc_protocol_t proto,
                        const void*        ctx,
                        const uint8_t*     tx,
                        size_t             tx_len,
                        uint8_t*           rx,
                        size_t             rx_max,
                        int                timeout_ms);

#endif
