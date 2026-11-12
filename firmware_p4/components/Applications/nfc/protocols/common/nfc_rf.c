// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "nfc_rf.h"

#include "esp_log.h"

#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "hb_nfc_spi.h"
#include "nfc_poller.h"

static const char *TAG = "NFC_RF";

#define NFC_RF_RATE_UNSUPPORTED    0xFF
#define NFC_RF_BITRATE_CODE_106    0x00U /**< Bitrate code for 106 kbps */
#define NFC_RF_BITRATE_CODE_212    0x01U /**< Bitrate code for 212 kbps */
#define NFC_RF_BITRATE_CODE_424    0x02U /**< Bitrate code for 424 kbps */
#define NFC_RF_BITRATE_CODE_848    0x03U /**< Bitrate code for 848 kbps */
#define NFC_RF_TX_BITRATE_SHIFT    4U    /**< Shift for TX bitrate in register */
#define NFC_RF_RX_BITRATE_MASK     0x03U /**< Mask for RX bitrate codes */
#define NFC_RF_AM_MOD_TABLE_SIZE   16U   /**< Number of AM modulation percentages */
#define NFC_RF_AM_MOD_INITIAL_DIFF 0xFF  /**< Initial difference for best match */
#define NFC_RF_TX_DRIVER_ADDR_MASK 0x0FU /**< Lower 4 bits = address */
#define NFC_RF_TX_DRIVER_MOD_MASK  0x0FU /**< Upper 4 bits = modulation */
#define NFC_RF_MODE_AM             0x04U
#define NFC_RF_B_RX_106            0x04U
#define NFC_RF_B_RX_212            0x02U
#define NFC_RF_B_RX_FAST           0x42U
#define NFC_RF_B_RX_AGC            0xFDU
#define NFC_RF_B_CORR_106_212      0x97U
#define NFC_RF_B_CORR_424          0xD7U
#define NFC_RF_B_CORR_848          0x47U
#define NFC_RF_FV_RX_FILTER        0x13U
#define NFC_RF_F_RX_AGC            0x2DU
#define NFC_RF_F_CORR              0x58U
#define NFC_RF_V_RX_AGC            0xEDU
#define NFC_RF_V_CORR1             0x13U
#define NFC_RF_V_CORR2             0x01U

static hb_nfc_err_t apply_receiver(uint8_t rx1, uint8_t rx2, uint8_t corr1, uint8_t corr2) {
  if (hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF1, rx1) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF2, rx2) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF3, 0) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF4, 0) != ESP_OK ||
      hb_nfc_spi_reg_write_b(ST25R3916_REG_B_CORR_CONF1, corr1) != ESP_OK ||
      hb_nfc_spi_reg_write_b(ST25R3916_REG_B_CORR_CONF2, corr2) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  return HB_NFC_OK;
}

static hb_nfc_err_t apply_type_b_receiver(nfc_rf_bitrate_t bitrate) {
  /* ST25R3916B RFAL receive profiles; AWS transmit settings do not apply to C99. */
  uint8_t rx1 = NFC_RF_B_RX_FAST;
  uint8_t corr1 = NFC_RF_B_CORR_106_212;
  switch (bitrate) {
    case NFC_RF_BR_106:
      rx1 = NFC_RF_B_RX_106;
      break;
    case NFC_RF_BR_212:
      rx1 = NFC_RF_B_RX_212;
      break;
    case NFC_RF_BR_424:
      corr1 = NFC_RF_B_CORR_424;
      break;
    case NFC_RF_BR_848:
      corr1 = NFC_RF_B_CORR_848;
      break;
    default:
      return HB_NFC_ERR_PARAM;
  }
  return apply_receiver(rx1, NFC_RF_B_RX_AGC, corr1, 0);
}

static uint8_t bitrate_code(nfc_rf_bitrate_t br) {
  switch (br) {
    case NFC_RF_BR_106:
      return NFC_RF_BITRATE_CODE_106;
    case NFC_RF_BR_212:
      return NFC_RF_BITRATE_CODE_212;
    case NFC_RF_BR_424:
      return NFC_RF_BITRATE_CODE_424;
    case NFC_RF_BR_848:
      return NFC_RF_BITRATE_CODE_848;
    default:
      return NFC_RF_RATE_UNSUPPORTED;
  }
}

hb_nfc_err_t nfc_rf_set_bitrate(nfc_rf_bitrate_t tx, nfc_rf_bitrate_t rx) {
  uint8_t txc = bitrate_code(tx);
  uint8_t rxc = bitrate_code(rx);
  if (txc == NFC_RF_RATE_UNSUPPORTED || rxc == NFC_RF_RATE_UNSUPPORTED)
    return HB_NFC_ERR_PARAM;

  uint8_t reg = (uint8_t)((txc << NFC_RF_TX_BITRATE_SHIFT) | (rxc & NFC_RF_RX_BITRATE_MASK));
  return hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, reg);
}

hb_nfc_err_t nfc_rf_set_bitrate_raw(uint8_t value) {
  return hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, value);
}

static uint8_t am_mod_code_from_percent(uint8_t percent) {
  /* ST25R3916B DS13541: the older ST25R3916 table is not interchangeable. */
  static const uint8_t tbl_percent[NFC_RF_AM_MOD_TABLE_SIZE] = {
      0, 8, 10, 11, 12, 13, 14, 15, 20, 25, 30, 40, 50, 60, 70, 82};

  uint8_t best = 0;
  uint8_t best_diff = NFC_RF_AM_MOD_INITIAL_DIFF;
  for (uint8_t i = 0; i < NFC_RF_AM_MOD_TABLE_SIZE; i++) {
    uint8_t p = tbl_percent[i];
    uint8_t diff = (p > percent) ? (p - percent) : (percent - p);
    if (diff < best_diff) {
      best_diff = diff;
      best = i;
    }
  }
  return best;
}

hb_nfc_err_t nfc_rf_set_am_modulation(uint8_t percent) {
  if (percent == 0)
    return HB_NFC_OK; /* keep current */

  uint8_t mod = am_mod_code_from_percent(percent);
  uint8_t reg = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_TX_DRIVER, &reg) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;

  reg = (uint8_t)((reg & NFC_RF_TX_DRIVER_ADDR_MASK) |
                  ((mod & NFC_RF_TX_DRIVER_MOD_MASK) << NFC_RF_TX_BITRATE_SHIFT));
  return hb_nfc_spi_reg_write(ST25R3916_REG_TX_DRIVER, reg);
}

hb_nfc_err_t nfc_rf_set_parity(bool tx_parity, bool rx_raw_parity) {
  uint8_t v = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443A, &v) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;

  if (tx_parity)
    v &= (uint8_t)~ST25R3916_ISO14443A_NO_TX_PAR;
  else
    v |= ST25R3916_ISO14443A_NO_TX_PAR;

  if (rx_raw_parity)
    v |= ST25R3916_ISO14443A_NO_RX_PAR;
  else
    v &= (uint8_t)~ST25R3916_ISO14443A_NO_RX_PAR;

  return hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, v);
}

void nfc_rf_set_timing(uint32_t guard_time_us, uint32_t fdt_min_us, bool validate_fdt) {
  nfc_poller_set_timing(guard_time_us, fdt_min_us, validate_fdt);
}

uint32_t nfc_rf_get_last_fdt_us(void) {
  return nfc_poller_get_last_fdt_us();
}

hb_nfc_err_t nfc_rf_apply(const nfc_rf_config_t *cfg) {
  if (cfg == NULL)
    return HB_NFC_ERR_PARAM;
  if (cfg->tech == NFC_RF_TECH_B && (bitrate_code(cfg->tx_rate) == NFC_RF_RATE_UNSUPPORTED ||
                                     bitrate_code(cfg->rx_rate) == NFC_RF_RATE_UNSUPPORTED))
    return HB_NFC_ERR_PARAM;
  if (cfg->tech == NFC_RF_TECH_F &&
      ((cfg->tx_rate != NFC_RF_BR_212 && cfg->tx_rate != NFC_RF_BR_424) ||
       (cfg->rx_rate != NFC_RF_BR_212 && cfg->rx_rate != NFC_RF_BR_424)))
    return HB_NFC_ERR_PARAM;

  hb_nfc_err_t err = HB_NFC_OK;
  // NFC-V checks its software-decoded CRC; other modes use the hardware checker.
  if (hb_nfc_spi_reg_modify(ST25R3916_REG_AUX_DEF, 0x80U, cfg->tech == NFC_RF_TECH_V ? 0x80U : 0) !=
      ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  switch (cfg->tech) {
    case NFC_RF_TECH_A:
      err = st25r3916_core_set_mode_nfca();
      if (err != HB_NFC_OK)
        return err;
      err = nfc_rf_set_bitrate(cfg->tx_rate, cfg->rx_rate);
      if (err != HB_NFC_OK)
        return err;
      err = nfc_rf_set_parity(cfg->tx_parity, cfg->rx_raw_parity);
      if (err != HB_NFC_OK)
        return err;
      break;
    case NFC_RF_TECH_B:
      err = apply_type_b_receiver(cfg->rx_rate);
      if (err != HB_NFC_OK)
        return err;
      if (hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_POLL_NFCB | NFC_RF_MODE_AM) !=
              ESP_OK ||
          hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443B, 0) != ESP_OK ||
          hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443B_FELICA, 0) != ESP_OK)
        return HB_NFC_ERR_INTERNAL;
      err = nfc_rf_set_bitrate(cfg->tx_rate, cfg->rx_rate);
      if (err != HB_NFC_OK)
        return err;
      break;
    case NFC_RF_TECH_F:
      err = apply_receiver(NFC_RF_FV_RX_FILTER, NFC_RF_F_RX_AGC, NFC_RF_F_CORR, 0);
      if (err != HB_NFC_OK)
        return err;
      if (hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_POLL_NFCF | NFC_RF_MODE_AM) !=
              ESP_OK ||
          hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443B_FELICA, 0) != ESP_OK)
        return HB_NFC_ERR_INTERNAL;
      err = nfc_rf_set_bitrate(cfg->tx_rate, cfg->rx_rate);
      if (err != HB_NFC_OK)
        return err;
      break;
    case NFC_RF_TECH_V:
      if (cfg->tx_rate != NFC_RF_BR_V_HIGH || cfg->rx_rate != NFC_RF_BR_V_HIGH)
        return HB_NFC_ERR_PARAM;
      // DS13541 tables 34-36: 424 kHz subcarrier, eight cycles/report, TX fc/128.
      if (hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_POLL_NFCV) != ESP_OK ||
          hb_nfc_spi_reg_write(ST25R3916_REG_STREAM_MODE, 0x38U) != ESP_OK)
        return HB_NFC_ERR_INTERNAL;
      err = apply_receiver(NFC_RF_FV_RX_FILTER, NFC_RF_V_RX_AGC, NFC_RF_V_CORR1, NFC_RF_V_CORR2);
      if (err != HB_NFC_OK)
        return err;
      break;
    default:
      return HB_NFC_ERR_PARAM;
  }

  if (cfg->tech != NFC_RF_TECH_A) {
    uint8_t mod = 0;
    if (hb_nfc_spi_reg_read_b(ST25R3916_REG_B_AUX_MOD, &mod) != ESP_OK ||
        hb_nfc_spi_reg_write_b(ST25R3916_REG_B_AUX_MOD, mod & ~0x88U) != ESP_OK)
      return HB_NFC_ERR_INTERNAL;
  }
  if (cfg->am_mod_percent > 0) {
    err = nfc_rf_set_am_modulation(cfg->am_mod_percent);
    if (err != HB_NFC_OK)
      return err;
  }

  nfc_rf_set_timing(cfg->guard_time_us, cfg->fdt_min_us, cfg->validate_fdt);

  ESP_LOGI(TAG,
           "RF cfg: tech=%d tx=%d rx=%d am=%u%% guard=%uus fdt=%uus",
           (int)cfg->tech,
           (int)cfg->tx_rate,
           (int)cfg->rx_rate,
           cfg->am_mod_percent,
           (unsigned)cfg->guard_time_us,
           (unsigned)cfg->fdt_min_us);
  return HB_NFC_OK;
}
