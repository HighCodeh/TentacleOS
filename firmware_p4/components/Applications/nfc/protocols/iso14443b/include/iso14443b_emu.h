/**
 * @file iso14443b_emu.h
 * @brief ISO14443B (NFC-B) target emulation with basic ISO-DEP/T4T APDUs.
 */
#ifndef ISO14443B_EMU_H
#define ISO14443B_EMU_H

#include <stdint.h>
#include "highboy_nfc_error.h"

typedef struct {
    uint8_t pupi[4];
    uint8_t app_data[4];
    uint8_t prot_info[3];
} iso14443b_emu_card_t;

/** Initialize default ATQB + T4T NDEF. */
hb_nfc_err_t iso14443b_emu_init_default(void);

/** Configure ST25R3916 as NFC-B target (ISO14443-3B). */
hb_nfc_err_t iso14443b_emu_configure_target(void);

/** Start emulation (enter SLEEP). */
hb_nfc_err_t iso14443b_emu_start(void);

/** Stop emulation. */
void iso14443b_emu_stop(void);

/** Run one emulation step (call in tight loop). */
void iso14443b_emu_run_step(void);

#endif /* ISO14443B_EMU_H */
