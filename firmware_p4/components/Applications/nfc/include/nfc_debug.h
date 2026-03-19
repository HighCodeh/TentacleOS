/**
 * @file nfc_debug.h
 * @brief NFC Debug tools CW, register dump, AAT sweep.
 */
#ifndef NFC_DEBUG_H
#define NFC_DEBUG_H

#include "highboy_nfc_error.h"

hb_nfc_err_t nfc_debug_cw_on(void);
void         nfc_debug_cw_off(void);
void         nfc_debug_dump_regs(void);
hb_nfc_err_t nfc_debug_aat_sweep(void);

#endif
