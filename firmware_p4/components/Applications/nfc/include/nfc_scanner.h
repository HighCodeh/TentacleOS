/**
 * @file nfc_scanner.h
 * @brief NFC Scanner auto-detect card technology (Phase 8).
 *
 * Probes NFC-A NFC-B NFC-F NFC-V in sequence.
 * Reports detected protocol(s) via callback.
 */
#ifndef NFC_SCANNER_H
#define NFC_SCANNER_H

#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

#define NFC_SCANNER_MAX_PROTOCOLS  4

typedef struct {
    hb_nfc_protocol_t protocols[NFC_SCANNER_MAX_PROTOCOLS];
    uint8_t           count;
} nfc_scanner_event_t;

typedef void (*nfc_scanner_cb_t)(nfc_scanner_event_t event, void* ctx);

typedef struct nfc_scanner nfc_scanner_t;

nfc_scanner_t* nfc_scanner_alloc(void);
void           nfc_scanner_free(nfc_scanner_t* s);
hb_nfc_err_t   nfc_scanner_start(nfc_scanner_t* s, nfc_scanner_cb_t cb, void* ctx);
void           nfc_scanner_stop(nfc_scanner_t* s);

#endif
