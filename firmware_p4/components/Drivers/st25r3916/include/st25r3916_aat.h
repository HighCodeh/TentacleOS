/**
 * @file st25r3916_aat.h
 * @brief ST25R3916 Automatic Antenna Tuning (Phase 4).
 */
#ifndef ST25R3916_AAT_H
#define ST25R3916_AAT_H

#include <stdint.h>
#include "highboy_nfc_error.h"

typedef struct {
    uint8_t dac_a;
    uint8_t dac_b;
    uint8_t amplitude;
    uint8_t phase;
} st25r_aat_result_t;

/** Run AAT calibration sweep. */
hb_nfc_err_t st25r_aat_calibrate(st25r_aat_result_t* result);

/** Load AAT values from NVS cache. */
hb_nfc_err_t st25r_aat_load_nvs(st25r_aat_result_t* result);

/** Save AAT values to NVS. */
hb_nfc_err_t st25r_aat_save_nvs(const st25r_aat_result_t* result);

#endif
