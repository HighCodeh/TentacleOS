/**
 * @file emu_diag.h
 * @brief Emulation Diagnostics Debug helper for ST25R3916 target mode.
 */
#ifndef EMU_DIAG_H
#define EMU_DIAG_H

#include "highboy_nfc_error.h"

/**
 * Full target mode diagnostic.
 * Tests field detection, multiple configs, PT Memory, oscillator.
 * Takes ~60 seconds. Share the FULL serial output!
 */
hb_nfc_err_t emu_diag_full(void);

/**
 * Monitor target interrupts for N seconds.
 */
void emu_diag_monitor(int seconds);

#endif
