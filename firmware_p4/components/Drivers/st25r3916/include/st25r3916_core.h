/**
 * @file st25r3916_core.h
 * @brief ST25R3916 Core init, reset, field, mode.
 */
#ifndef ST25R3916_CORE_H
#define ST25R3916_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc.h"

hb_nfc_err_t st25r_init(const highboy_nfc_config_t* cfg);
void         st25r_deinit(void);
hb_nfc_err_t st25r_check_id(uint8_t* id, uint8_t* type, uint8_t* rev);
hb_nfc_err_t st25r_field_on(void);
void         st25r_field_off(void);
bool         st25r_field_is_on(void);
hb_nfc_err_t st25r_field_cycle(void);
hb_nfc_err_t st25r_set_mode_nfca(void);

/** Dump all 64 registers to log. */
void st25r_dump_regs(void);

#endif
