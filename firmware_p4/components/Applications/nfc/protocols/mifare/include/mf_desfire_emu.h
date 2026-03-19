/**
 * @file mf_desfire_emu.h
 * @brief Minimal MIFARE DESFire (native) APDU emulation helpers.
 */
#ifndef MF_DESFIRE_EMU_H
#define MF_DESFIRE_EMU_H

#include <stdint.h>
#include <stdbool.h>

/** Reset DESFire emu session state. */
void mf_desfire_emu_reset(void);

/**
 * Handle a DESFire native APDU (CLA=0x90).
 *
 * @param apdu     input APDU buffer
 * @param apdu_len input length
 * @param out      output buffer (data + SW1 SW2)
 * @param out_len  output length
 * @return true if handled, false if not a DESFire APDU
 */
bool mf_desfire_emu_handle_apdu(const uint8_t* apdu, int apdu_len,
                                uint8_t* out, int* out_len);

#endif /* MF_DESFIRE_EMU_H */
