/**
 * @file iso_dep.h
 * @brief ISO-DEP (ISO14443-4) RATS, I-Block, chaining (Phase 6).
 */
#ifndef ISO_DEP_H
#define ISO_DEP_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"

/** Send RATS and receive ATS (fills dep->ats, dep->fsc, dep->fwi). */
hb_nfc_err_t iso_dep_rats(uint8_t fsdi, uint8_t cid, nfc_iso_dep_data_t* dep);

/** Optional PPS (best-effort, keeps 106 kbps when dsi/dri = 0). */
hb_nfc_err_t iso_dep_pps(uint8_t cid, uint8_t dsi, uint8_t dri);

/** Exchange ISO-DEP I-Blocks with basic chaining and WTX handling. */
int iso_dep_transceive(const nfc_iso_dep_data_t* dep,
                        const uint8_t*           tx,
                        size_t                   tx_len,
                        uint8_t*                 rx,
                        size_t                   rx_max,
                        int                      timeout_ms);

/** APDU transceive convenience wrapper (same as iso_dep_transceive). */
int iso_dep_apdu_transceive(const nfc_iso_dep_data_t* dep,
                             const uint8_t*           apdu,
                             size_t                   apdu_len,
                             uint8_t*                 rx,
                             size_t                   rx_max,
                             int                      timeout_ms);

#endif
