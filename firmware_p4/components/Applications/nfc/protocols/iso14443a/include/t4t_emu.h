/**
 * @file t4t_emu.h
 * @brief ISO14443-4 / T4T emulation (ISO-DEP target).
 */
#ifndef T4T_EMU_H
#define T4T_EMU_H

#include "highboy_nfc_error.h"

/** Initialize default NDEF + CC. */
hb_nfc_err_t t4t_emu_init_default(void);

/** Configure ST25R3916 as NFC-A target for ISO-DEP. */
hb_nfc_err_t t4t_emu_configure_target(void);

/** Start emulation (enter SLEEP). */
hb_nfc_err_t t4t_emu_start(void);

/** Stop emulation. */
void t4t_emu_stop(void);

/** Run one emulation step (call in tight loop). */
void t4t_emu_run_step(void);

#endif /* T4T_EMU_H */
