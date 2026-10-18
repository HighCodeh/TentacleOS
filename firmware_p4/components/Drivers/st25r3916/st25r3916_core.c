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

#include "st25r3916_core.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_aat.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_gpio.h"
#include "hb_nfc_timer.h"
#include "rf_frontend.h"

static const char *TAG = "ST25R3916_CORE";

typedef struct {
  bool is_init;
  bool is_field_on;
  bool has_field_fault;
  highboy_nfc_config_t config;
} st25r3916_core_drv_state_t;

static st25r3916_core_drv_state_t s_drv = {0};

esp_err_t st25r3916_core_init(const highboy_nfc_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  s_drv.config = *config;

  if (hb_nfc_gpio_init(config->pin_irq) != ESP_OK) {
    return ESP_FAIL;
  }

  hb_nfc_spi_config_t spi_cfg = {
      .spi_host = config->spi_host,
      .pin_mosi = config->pin_mosi,
      .pin_miso = config->pin_miso,
      .pin_sclk = config->pin_sclk,
      .pin_cs = config->pin_cs,
      .mode = config->spi_mode,
      .clock_hz = config->spi_clock_hz,
  };
  if (hb_nfc_spi_init(&spi_cfg) != ESP_OK) {
    hb_nfc_gpio_deinit();
    return ESP_FAIL;
  }

  hb_nfc_timer_delay_ms(ST25R3916_SPI_INIT_DELAY_MS);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_SET_DEFAULT);
  /* Keep strong SPI output drive, but leave the ST25R3916 MISO pull-downs
   * disabled.  ST's reference enables them for a dedicated SPI connection;
   * High Boy shares MISO with CC1101, SX1262 and QMI8658, so the pull-downs
   * hide a floating-bus diagnostic and load every other peripheral.  SUP3V
   * is applied after the oscillator is stable. */
  const uint8_t io_conf2 = ST25R3916_IO_CONF2_IO_DRV;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_IO_CONF2, io_conf2) != ESP_OK) {
    hb_nfc_spi_deinit();
    hb_nfc_gpio_deinit();
    return ESP_FAIL;
  }
  /* SET_DEFAULT leaves the oscillator disabled. EN must be set before the
   * oscillator-stable IRQ can ever arrive. This does not enable the RF field. */
  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_EN) != ESP_OK) {
    hb_nfc_spi_deinit();
    hb_nfc_gpio_deinit();
    return ESP_FAIL;
  }

  bool osc_ok = false;
  for (int i = 0; i < ST25R3916_OSC_POLL_MAX; i++) {
    uint8_t aux = 0;
    hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
    if (aux & ST25R3916_AUX_DISP_OSC_OK) {
      osc_ok = true;
      break;
    }
    hb_nfc_timer_delay_us(ST25R3916_OSC_POLL_INTERVAL_US);
  }

  if (!osc_ok) {
    ESP_LOGW(TAG, "OSC ready timeout");
  }

  /* High Boy supplies the reader IC from 3.3 V.  Configure this only after
   * the oscillator is stable, as done by the ST reference driver. */
  if (hb_nfc_spi_reg_modify(
          ST25R3916_REG_IO_CONF2, ST25R3916_IO_CONF2_SUP3V, ST25R3916_IO_CONF2_SUP3V) != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_SHUTDOWN);
    hb_nfc_spi_deinit();
    hb_nfc_gpio_deinit();
    return ESP_FAIL;
  }

  /* ST25R3916B reference analog initialization. AAT DAC values are omitted:
   * High Boy uses a fixed matching network and leaves AAT_A/AAT_B unconnected. */
  hb_nfc_spi_reg_write(ST25R3916_REG_IO_CONF1, 0x07);
  hb_nfc_spi_reg_modify(ST25R3916_REG_TX_DRIVER, ST25R3916_TX_DRIVER_D_RES_MASK, 0x00);
  hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_ACT, 0x13);
  hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_DEACT, 0x02);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_EMD_SUP_CONF, 0x40);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_RES_AM_MOD, 0x80);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_AUX_MOD, 0x94);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_AWS_CONF1, 0x09);

  /* Reading the IRQ registers clears oscillator/calibration IRQs left from
   * initialization, so the first RF exchange starts from a known state. */
  uint8_t ignored_irq = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &ignored_irq);
  hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &ignored_irq);
  hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &ignored_irq);
  hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &ignored_irq);

  uint8_t id, type, rev;
  if (st25r3916_core_check_id(&id, &type, &rev) != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_SHUTDOWN);
    hb_nfc_spi_deinit();
    hb_nfc_gpio_deinit();
    return ESP_FAIL;
  }

  /* Reset the regulator result latch before automatic adjustment. */
  hb_nfc_spi_reg_modify(
      ST25R3916_REG_REGULATOR_CTRL, ST25R3916_REGULATOR_REG_S, ST25R3916_REGULATOR_REG_S);
  hb_nfc_spi_reg_modify(ST25R3916_REG_REGULATOR_CTRL, ST25R3916_REGULATOR_REG_S, 0x00);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS);
  hb_nfc_timer_delay_ms(ST25R3916_REGULATOR_DELAY_MS);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_CALIBRATE_DRIVER);
  hb_nfc_timer_delay_ms(ST25R3916_DRIVER_CAL_DELAY_MS);

  /* Calibration direct commands assert DCT; clear it before normal polling. */
  hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &ignored_irq);
  hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &ignored_irq);
  hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &ignored_irq);
  hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &ignored_irq);

  s_drv.is_init = true;
  ESP_LOGI(TAG, "Init OK (Chip 0x%02X)", id);
  return ESP_OK;
}

esp_err_t st25r3916_core_deinit(void) {
  if (!s_drv.is_init) {
    return ESP_OK;
  }
  esp_err_t err = st25r3916_core_field_off();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Keeping NFC initialized until the RF field can be disabled");
    return err;
  }
  hb_nfc_spi_reg_modify(
      ST25R3916_REG_IO_CONF2, ST25R3916_IO_CONF2_MISO_PD1 | ST25R3916_IO_CONF2_MISO_PD2, 0);
  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_SHUTDOWN);
  hb_nfc_spi_deinit();
  hb_nfc_gpio_deinit();
  s_drv.is_init = false;
  s_drv.is_field_on = false;
  return ESP_OK;
}

esp_err_t st25r3916_core_check_id(uint8_t *out_id, uint8_t *out_type, uint8_t *out_rev) {
  uint8_t val = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_IC_IDENTITY, &val) != ESP_OK) {
    return ESP_FAIL;
  }

  if (val == 0x00 || val == 0xFF) {
    ESP_LOGE(TAG, "Hardware communication error (0x%02X)", val);
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t ic_type = (val >> ST25R3916_IC_TYPE_SHIFT) & 0x1F;
  uint8_t ic_rev = val & ST25R3916_IC_REV_MASK;

  if (ic_type != ST25R3916_IC_TYPE_EXP) {
    ESP_LOGE(TAG,
             "Unexpected chip identity 0x%02X (type 0x%02X, expected ST25R3916B type 0x%02X)",
             val,
             ic_type,
             ST25R3916_IC_TYPE_EXP);
    return ESP_ERR_NOT_FOUND;
  }

  if (out_id != NULL)
    *out_id = val;
  if (out_type != NULL)
    *out_type = ic_type;
  if (out_rev != NULL)
    *out_rev = ic_rev;

  return ESP_OK;
}

esp_err_t st25r3916_core_field_on(void) {
  if (!s_drv.is_init) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_drv.has_field_fault) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_drv.is_field_on) {
    return ESP_OK;
  }

  esp_err_t err = rf_frontend_claim(RF_FRONTEND_NFC_13MHZ);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Antenna busy in %s mode", rf_frontend_mode_name(rf_frontend_get_mode()));
    return err;
  }
  // A failed transfer may already have enabled TX; retain ownership until OFF succeeds.
  s_drv.is_field_on = true;
  s_drv.has_field_fault = true;
  err = hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_FIELD_ON);
  if (err != ESP_OK) {
    (void)st25r3916_core_field_off();
    return err;
  }
  bool osc_ok = false;
  for (int i = 0; i < ST25R3916_OSC_POLL_MAX; i++) {
    uint8_t aux = 0;
    if (hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux) == ESP_OK &&
        (aux & ST25R3916_AUX_DISP_OSC_OK) != 0) {
      osc_ok = true;
      break;
    }
    hb_nfc_timer_delay_us(ST25R3916_OSC_POLL_INTERVAL_US);
  }
  if (!osc_ok) {
    ESP_LOGW(TAG, "RF field start: oscillator did not stabilize");
    (void)st25r3916_core_field_off();
    return ESP_ERR_TIMEOUT;
  }
  hb_nfc_timer_delay_ms(ST25R3916_FIELD_ON_DELAY_MS);
  err = hb_nfc_spi_direct_cmd(ST25R3916_CMD_RESET_RX_GAIN);
  if (err != ESP_OK) {
    (void)st25r3916_core_field_off();
    return err;
  }

  s_drv.has_field_fault = false;
  return ESP_OK;
}

esp_err_t st25r3916_core_field_off(void) {
  if (!s_drv.is_init) {
    return ESP_OK;
  }
  esp_err_t err = hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_FIELD_OFF);
  if (err != ESP_OK) {
    s_drv.is_field_on = true;
    s_drv.has_field_fault = true;
    ESP_LOGE(TAG, "RF field OFF failed; keeping shared antenna reserved");
    return err;
  }
  s_drv.is_field_on = false;
  s_drv.has_field_fault = false;
  return ESP_OK;
}

bool st25r3916_core_field_is_on(void) {
  return s_drv.is_field_on;
}

esp_err_t st25r3916_core_field_cycle(void) {
  if (!s_drv.is_init) {
    return ESP_ERR_INVALID_STATE;
  }

  st25r3916_core_field_off();
  hb_nfc_timer_delay_ms(ST25R3916_FIELD_CYCLE_DELAY_MS);
  return st25r3916_core_field_on();
}

esp_err_t st25r3916_core_set_mode_nfca(void) {
  if (!s_drv.is_init) {
    return ESP_ERR_INVALID_STATE;
  }

  hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_POLL_NFCA);
  hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, 0x00);

  /* High Boy NFC-A 106 kbit/s receive profile.  The custom matching network
   * selected cards and ATS frames more reliably with the I/Q+mixer path than
   * the NFC06A1 AM/PM peak-detector value (0x2D).  Dynamic squelch and AGC
   * remain enabled. */
  hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF1, 0x08);
  hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF2, 0xED);
  hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF3, 0x00);
  hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF4, 0x00);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_CORR_CONF1, 0x51);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_CORR_CONF2, 0x00);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_AWS_TIME1, 0x01);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_AWS_TIME3, 0x79);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_AWS_TIME4, 0x07);
  hb_nfc_spi_reg_write_b(ST25R3916_REG_B_AWS_CONF2, 0x18);

  /* OOK profile from ST RFAL: maximum field drive (d_res=0) and AWS index. */
  hb_nfc_spi_reg_write(ST25R3916_REG_TX_DRIVER, 0xF0);

  uint8_t iso_config = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443A, &iso_config);
  iso_config &= (uint8_t)~(ST25R3916_ISO14443A_NO_TX_PAR | ST25R3916_ISO14443A_ANTCL);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso_config);

  return ESP_OK;
}

void st25r3916_core_dump_regs(void) {
  if (!s_drv.is_init) {
    return;
  }

  uint8_t regs[ST25R3916_REG_DUMP_COUNT];
  for (int i = 0; i < ST25R3916_REG_DUMP_COUNT; i++) {
    hb_nfc_spi_reg_read((uint8_t)i, &regs[i]);
  }

  for (int r = 0; r < ST25R3916_REG_DUMP_COUNT; r += ST25R3916_REG_DUMP_STRIDE) {
    char line[(ST25R3916_REG_DUMP_STRIDE * 3) + 1];
    size_t pos = 0;
    for (int i = 0; i < ST25R3916_REG_DUMP_STRIDE; i++) {
      pos += (size_t)snprintf(line + pos,
                              sizeof(line) - pos,
                              "%02X%s",
                              regs[r + i],
                              i + 1 < ST25R3916_REG_DUMP_STRIDE ? " " : "");
    }
    ESP_LOGI(TAG, "Dump A%02X: %s", r, line);
  }

  uint8_t corr1 = 0, corr2 = 0, aux_mod = 0, res_am = 0, reg_disp = 0, aws1 = 0, aws2 = 0;
  hb_nfc_spi_reg_read_b(ST25R3916_REG_B_CORR_CONF1, &corr1);
  hb_nfc_spi_reg_read_b(ST25R3916_REG_B_CORR_CONF2, &corr2);
  hb_nfc_spi_reg_read_b(ST25R3916_REG_B_AUX_MOD, &aux_mod);
  hb_nfc_spi_reg_read_b(ST25R3916_REG_B_RES_AM_MOD, &res_am);
  hb_nfc_spi_reg_read_b(ST25R3916_REG_B_REGULATOR_DISPLAY, &reg_disp);
  hb_nfc_spi_reg_read_b(ST25R3916_REG_B_AWS_CONF1, &aws1);
  hb_nfc_spi_reg_read_b(ST25R3916_REG_B_AWS_CONF2, &aws2);
  ESP_LOGI(TAG,
           "Dump B: CORR=%02X/%02X AUX_MOD=%02X RES_AM=%02X REG_DISP=%02X AWS=%02X/%02X",
           corr1,
           corr2,
           aux_mod,
           res_am,
           reg_disp,
           aws1,
           aws2);
}
