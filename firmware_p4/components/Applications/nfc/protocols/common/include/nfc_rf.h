/**
 * @file nfc_rf.h
 * @brief RF layer configuration (Phase 2): bitrate, modulation, parity, timing.
 */
#ifndef NFC_RF_H
#define NFC_RF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "highboy_nfc_error.h"

typedef enum {
    NFC_RF_TECH_A = 0,
    NFC_RF_TECH_B,
    NFC_RF_TECH_F,
    NFC_RF_TECH_V,
} nfc_rf_tech_t;

typedef enum {
    NFC_RF_BR_106 = 0,
    NFC_RF_BR_212,
    NFC_RF_BR_424,
    NFC_RF_BR_848,
    NFC_RF_BR_V_LOW,   /* NFC-V low data rate (6.62 kbps RX / 1.65 kbps TX) */
    NFC_RF_BR_V_HIGH,  /* NFC-V high data rate (26.48 kbps) */
} nfc_rf_bitrate_t;

typedef struct {
    nfc_rf_tech_t   tech;
    nfc_rf_bitrate_t tx_rate;
    nfc_rf_bitrate_t rx_rate;
    uint8_t         am_mod_percent;  /* 0 = keep current */
    bool            tx_parity;       /* ISO14443A: generate parity */
    bool            rx_raw_parity;   /* ISO14443A: receive parity bits in FIFO (no check) */
    uint32_t        guard_time_us;   /* optional delay before TX */
    uint32_t        fdt_min_us;      /* optional minimum FDT */
    bool            validate_fdt;    /* log warning if FDT < min */
} nfc_rf_config_t;

/** Apply RF configuration for a given technology. */
hb_nfc_err_t nfc_rf_apply(const nfc_rf_config_t* cfg);

/** Set TX/RX bitrate (A/B/F). */
hb_nfc_err_t nfc_rf_set_bitrate(nfc_rf_bitrate_t tx, nfc_rf_bitrate_t rx);

/** Set raw BIT_RATE register (advanced use). */
hb_nfc_err_t nfc_rf_set_bitrate_raw(uint8_t value);

/** Set AM modulation index in percent (5..40). */
hb_nfc_err_t nfc_rf_set_am_modulation(uint8_t percent);

/** ISO14443A parity control. */
hb_nfc_err_t nfc_rf_set_parity(bool tx_parity, bool rx_raw_parity);

/** Configure timing (guard time + FDT validation). */
void nfc_rf_set_timing(uint32_t guard_time_us, uint32_t fdt_min_us, bool validate_fdt);

/** Returns last measured FDT in microseconds. */
uint32_t nfc_rf_get_last_fdt_us(void);

#endif
