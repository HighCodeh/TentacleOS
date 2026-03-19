// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/**
 * @file nfc_rf.c
 * @brief RF layer configuration (Phase 2).
 */
#include "nfc_rf.h"

#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "hb_nfc_spi.h"
#include "nfc_poller.h"

#include "esp_log.h"

static const char* TAG = "nfc_rf";

static uint8_t bitrate_code(nfc_rf_bitrate_t br)
{
    switch (br) {
    case NFC_RF_BR_106: return 0x00;
    case NFC_RF_BR_212: return 0x01;
    case NFC_RF_BR_424: return 0x02;
    case NFC_RF_BR_848: return 0x03;
    default:            return 0xFF;
    }
}

hb_nfc_err_t nfc_rf_set_bitrate(nfc_rf_bitrate_t tx, nfc_rf_bitrate_t rx)
{
    uint8_t txc = bitrate_code(tx);
    uint8_t rxc = bitrate_code(rx);
    if (txc == 0xFF || rxc == 0xFF) return HB_NFC_ERR_PARAM;

    uint8_t reg = (uint8_t)((txc << 4) | (rxc & 0x03U));
    return hb_spi_reg_write(REG_BIT_RATE, reg);
}

hb_nfc_err_t nfc_rf_set_bitrate_raw(uint8_t value)
{
    return hb_spi_reg_write(REG_BIT_RATE, value);
}

static uint8_t am_mod_code_from_percent(uint8_t percent)
{
    static const uint8_t tbl_percent[16] = {
        5, 6, 7, 8, 9, 10, 11, 12,
        13, 14, 15, 17, 19, 22, 26, 40
    };

    uint8_t best = 0;
    uint8_t best_diff = 0xFF;
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t p = tbl_percent[i];
        uint8_t diff = (p > percent) ? (p - percent) : (percent - p);
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    return best;
}

hb_nfc_err_t nfc_rf_set_am_modulation(uint8_t percent)
{
    if (percent == 0) return HB_NFC_OK; /* keep current */

    uint8_t mod = am_mod_code_from_percent(percent);
    uint8_t reg = 0;
    if (hb_spi_reg_read(REG_TX_DRIVER, &reg) != HB_NFC_OK) return HB_NFC_ERR_INTERNAL;

    reg = (uint8_t)((reg & 0x0FU) | ((mod & 0x0FU) << 4));
    return hb_spi_reg_write(REG_TX_DRIVER, reg);
}

hb_nfc_err_t nfc_rf_set_parity(bool tx_parity, bool rx_raw_parity)
{
    uint8_t v = 0;
    if (hb_spi_reg_read(REG_ISO14443A, &v) != HB_NFC_OK) return HB_NFC_ERR_INTERNAL;

    if (tx_parity) v &= (uint8_t)~ISO14443A_NO_TX_PAR;
    else           v |= ISO14443A_NO_TX_PAR;

    if (rx_raw_parity) v |= ISO14443A_NO_RX_PAR;
    else               v &= (uint8_t)~ISO14443A_NO_RX_PAR;

    return hb_spi_reg_write(REG_ISO14443A, v);
}

void nfc_rf_set_timing(uint32_t guard_time_us, uint32_t fdt_min_us, bool validate_fdt)
{
    nfc_poller_set_timing(guard_time_us, fdt_min_us, validate_fdt);
}

uint32_t nfc_rf_get_last_fdt_us(void)
{
    return nfc_poller_get_last_fdt_us();
}

hb_nfc_err_t nfc_rf_apply(const nfc_rf_config_t* cfg)
{
    if (!cfg) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = HB_NFC_OK;

    switch (cfg->tech) {
    case NFC_RF_TECH_A:
        err = st25r_set_mode_nfca();
        if (err != HB_NFC_OK) return err;
        err = nfc_rf_set_bitrate(cfg->tx_rate, cfg->rx_rate);
        if (err != HB_NFC_OK) return err;
        err = nfc_rf_set_parity(cfg->tx_parity, cfg->rx_raw_parity);
        if (err != HB_NFC_OK) return err;
        break;
    case NFC_RF_TECH_B:
        hb_spi_reg_write(REG_MODE, MODE_POLL_NFCB);
        hb_spi_reg_write(REG_ISO14443B, 0x00U);
        hb_spi_reg_write(REG_ISO14443B_FELICA, 0x00U);
        err = nfc_rf_set_bitrate(cfg->tx_rate, cfg->rx_rate);
        if (err != HB_NFC_OK) return err;
        break;
    case NFC_RF_TECH_F:
        hb_spi_reg_write(REG_MODE, MODE_POLL_NFCF);
        hb_spi_reg_write(REG_ISO14443B_FELICA, 0x00U);
        err = nfc_rf_set_bitrate(cfg->tx_rate, cfg->rx_rate);
        if (err != HB_NFC_OK) return err;
        break;
    case NFC_RF_TECH_V:
        hb_spi_reg_write(REG_MODE, MODE_POLL_NFCV);
        hb_spi_reg_write(REG_ISO14443B, 0x00U);
        hb_spi_reg_write(REG_ISO14443B_FELICA, 0x00U);
        if (cfg->tx_rate == NFC_RF_BR_V_HIGH && cfg->rx_rate == NFC_RF_BR_V_HIGH) {
            err = nfc_rf_set_bitrate_raw(0x00U); /* NFC-V high data rate */
        } else if (cfg->tx_rate == NFC_RF_BR_V_LOW && cfg->rx_rate == NFC_RF_BR_V_LOW) {
            err = nfc_rf_set_bitrate_raw(0x01U); /* NFC-V low data rate (best-effort) */
        } else {
            err = HB_NFC_ERR_PARAM;
        }
        if (err != HB_NFC_OK) return err;
        break;
    default:
        return HB_NFC_ERR_PARAM;
    }

    if (cfg->am_mod_percent > 0) {
        err = nfc_rf_set_am_modulation(cfg->am_mod_percent);
        if (err != HB_NFC_OK) return err;
    }

    nfc_rf_set_timing(cfg->guard_time_us, cfg->fdt_min_us, cfg->validate_fdt);

    ESP_LOGI(TAG, "RF cfg: tech=%d tx=%d rx=%d am=%u%% guard=%uus fdt=%uus",
             (int)cfg->tech, (int)cfg->tx_rate, (int)cfg->rx_rate,
             cfg->am_mod_percent, (unsigned)cfg->guard_time_us,
             (unsigned)cfg->fdt_min_us);
    return HB_NFC_OK;
}
