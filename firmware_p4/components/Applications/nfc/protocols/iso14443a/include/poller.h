/**
 * @file poller.h
 * @brief ISO14443A Poller REQA/WUPA, anti-collision, SELECT.
 *
 * All functions are direct refactors of the working code.
 */
#ifndef ISO14443A_POLLER_H
#define ISO14443A_POLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

/**
 * Send REQA or WUPA and get ATQA.
 * Tries REQA first; if no response, waits 5ms and sends WUPA.
 * Exact logic from working code st25r_reqA_or_wupa().
 */
hb_nfc_err_t iso14443a_poller_activate(uint8_t atqa[2]);

/**
 * Full card activation: REQA anti-collision SELECT (all cascade levels).
 * Fills the nfc_iso14443a_data_t struct with UID, ATQA, SAK.
 * Exact logic from working code's app_main() card selection sequence.
 */
hb_nfc_err_t iso14443a_poller_select(nfc_iso14443a_data_t* card);

/**
 * Re-select a card (WUPA anticoll select).
 * From working code's reselect() macro.
 */
hb_nfc_err_t iso14443a_poller_reselect(nfc_iso14443a_data_t* card);

/**
 * Send HLTA (halt). The tag should enter HALT state.
 */
hb_nfc_err_t iso14443a_poller_halt(void);

/**
 * Discover multiple NFC-A tags by iterating REQA + select + HLTA.
 * Returns number of cards found (up to max_cards).
 */
int iso14443a_poller_select_all(nfc_iso14443a_data_t* out, size_t max_cards);

/**
 * Send REQA only. Returns 2 on success (ATQA bytes).
 */
int iso14443a_poller_reqa(uint8_t atqa[2]);

/**
 * Send WUPA only.
 */
int iso14443a_poller_wupa(uint8_t atqa[2]);

/**
 * Anti-collision for one cascade level.
 * @param sel SEL_CL1 (0x93), SEL_CL2 (0x95), or SEL_CL3 (0x97).
 * @param uid_cl Output: 4 UID bytes + BCC (5 bytes total).
 */
int iso14443a_poller_anticoll(uint8_t sel, uint8_t uid_cl[5]);

/**
 * SELECT for one cascade level.
 * @param sel SEL_CL1/CL2/CL3.
 * @param uid_cl 5 bytes from anti-collision.
 * @param sak Output: SAK byte.
 */
int iso14443a_poller_sel(uint8_t sel, const uint8_t uid_cl[5], uint8_t* sak);

#endif
