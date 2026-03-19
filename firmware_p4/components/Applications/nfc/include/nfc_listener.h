/**
 * @file nfc_listener.h
 * @brief NFC Listener card emulation control (Phase 9).
 */
#ifndef NFC_LISTENER_H
#define NFC_LISTENER_H

#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"
#include "mf_classic_emu.h"

hb_nfc_err_t nfc_listener_start(const hb_nfc_card_data_t* card);

/**
 * Start emulation from a pre-loaded dump (blocks + keys already filled).
 * Use this after mf_classic_read_full() has populated the mfc_emu_card_data_t.
 */
hb_nfc_err_t nfc_listener_start_emu(const mfc_emu_card_data_t* emu);

void nfc_listener_stop(void);

#endif
