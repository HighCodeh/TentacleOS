/**
 * @file nfc_tcl.c
 * @brief ISO14443-4 (T=CL) transport wrapper for A/B.
 */
#include "nfc_tcl.h"

#include "iso_dep.h"
#include "iso14443b.h"

int nfc_tcl_transceive(hb_nfc_protocol_t proto,
                        const void*        ctx,
                        const uint8_t*     tx,
                        size_t             tx_len,
                        uint8_t*           rx,
                        size_t             rx_max,
                        int                timeout_ms)
{
    if (!ctx || !tx || !rx || tx_len == 0) return 0;

    if (proto == HB_PROTO_ISO14443_4A) {
        return iso_dep_transceive((const nfc_iso_dep_data_t*)ctx,
                                   tx, tx_len, rx, rx_max, timeout_ms);
    }
    if (proto == HB_PROTO_ISO14443_4B) {
        return iso14443b_tcl_transceive((const nfc_iso14443b_data_t*)ctx,
                                         tx, tx_len, rx, rx_max, timeout_ms);
    }

    return 0;
}
