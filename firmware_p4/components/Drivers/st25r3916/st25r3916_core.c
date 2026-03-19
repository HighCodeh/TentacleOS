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
 * @file st25r3916_core.c
 * @brief ST25R3916 core: init, field control, mode, diagnostics.
 */
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_aat.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_gpio.h"
#include "hb_nfc_timer.h"

#include "esp_log.h"

static const char* TAG = "st25r";

static struct {
    bool init;
    bool field;
    highboy_nfc_config_t cfg;
} s_drv = { 0 };

hb_nfc_err_t st25r_init(const highboy_nfc_config_t* cfg)
{
    if (!cfg) return HB_NFC_ERR_PARAM;
    hb_nfc_err_t err;

    s_drv.cfg = *cfg;

    err = hb_gpio_init(cfg->pin_irq);
    if (err != HB_NFC_OK) return err;

    err = hb_spi_init(cfg->spi_host, cfg->pin_mosi, cfg->pin_miso,
                       cfg->pin_sclk, cfg->pin_cs, cfg->spi_mode,
                       cfg->spi_clock_hz);
    if (err != HB_NFC_OK) {
        hb_gpio_deinit();
        return err;
    }

    hb_delay_ms(5);

    hb_spi_direct_cmd(CMD_SET_DEFAULT);

    /* Wait for oscillator ready (REG_MAIN_INT bit 7 = IRQ_MAIN_OSC).
     * Datasheet Sec. 8.1: OSC stabilises in ~1ms typical, 5ms worst case.
     * Poll up to 10ms before giving up (blind wait is not reliable on cold PCBs). */
    {
        int osc_ok = 0;
        for (int i = 0; i < 100; i++) {
            uint8_t irq = 0;
            hb_spi_reg_read(REG_MAIN_INT, &irq);
            if (irq & IRQ_MAIN_OSC) { osc_ok = 1; break; }
            hb_delay_us(100);
        }
        if (!osc_ok) {
            ESP_LOGW(TAG, "OSC ready timeout - continuing anyway");
        }
    }

    uint8_t id, type, rev;
    err = st25r_check_id(&id, &type, &rev);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "Chip ID check FAILED");
        hb_spi_deinit();
        hb_gpio_deinit();
        return err;
    }

    /* Calibrate internal regulator and TX driver (datasheet Sec. 8.1.3).
     * Must be done after oscillator is stable and before RF field is enabled. */
    hb_spi_direct_cmd(CMD_ADJUST_REGULATORS);
    hb_delay_ms(6);  /* 6ms for regulator settling */

    hb_spi_direct_cmd(CMD_CALIBRATE_DRIVER);
    hb_delay_ms(1);  /* 1ms for driver calibration */

    s_drv.init = true;
    ESP_LOGI(TAG, "Init OK (chip 0x%02X type=0x%02X rev=%u)", id, type, rev);
    return HB_NFC_OK;
}

void st25r_deinit(void)
{
    if (!s_drv.init) return;
    st25r_field_off();
    hb_spi_deinit();
    hb_gpio_deinit();
    s_drv.init = false;
    s_drv.field = false;
}

/**
 * Read and parse IC Identity:
 *  ic_type = (id >> 3) & 0x1F
 *  ic_rev = id & 0x07
 */
hb_nfc_err_t st25r_check_id(uint8_t* id, uint8_t* type, uint8_t* rev)
{
    uint8_t val;
    hb_nfc_err_t err = hb_spi_reg_read(REG_IC_IDENTITY, &val);
    if (err != HB_NFC_OK) return HB_NFC_ERR_CHIP_ID;

    if (val == 0x00 || val == 0xFF) {
        ESP_LOGE(TAG, "Bad IC ID 0x%02X - check SPI wiring!", val);
        return HB_NFC_ERR_CHIP_ID;
    }

    uint8_t ic_type = (val >> 3) & 0x1F;
    uint8_t ic_rev  = val & 0x07;

    if (ic_type != ST25R3916_IC_TYPE_EXP) {
        /* ST25R3916B (or unknown variant) - command table may differ. */
        ESP_LOGW(TAG, "Unexpected IC type 0x%02X (expected 0x%02X): "
                      "may be ST25R3916B - verify command codes",
                 ic_type, ST25R3916_IC_TYPE_EXP);
    }

    if (id)   *id   = val;
    if (type) *type = ic_type;
    if (rev)  *rev  = ic_rev;

    ESP_LOGI(TAG, "IC_IDENTITY: 0x%02X (type=0x%02X rev=%u)", val, ic_type, ic_rev);
    return HB_NFC_OK;
}

hb_nfc_err_t st25r_field_on(void)
{
    if (!s_drv.init) return HB_NFC_ERR_INTERNAL;
    if (s_drv.field) return HB_NFC_OK;

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_FIELD_ON);
    hb_delay_ms(5);
    hb_spi_direct_cmd(CMD_RESET_RX_GAIN);

    s_drv.field = true;
    ESP_LOGI(TAG, "RF field ON");
    return HB_NFC_OK;
}

void st25r_field_off(void)
{
    if (!s_drv.init) return;
    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    s_drv.field = false;
}

bool st25r_field_is_on(void)
{
    return s_drv.field;
}

/**
 * Field cycle: briefly toggle field OFF then ON.
 *
 * After Crypto1 authentication, the card is in AUTHENTICATED state and
 * will NOT respond to WUPA. The only reliable way to reset it is to
 * power-cycle by removing the RF field briefly.
 *
 * Timing: 5ms off + 5ms on (minimum 1ms off required by ISO14443).
 */
hb_nfc_err_t st25r_field_cycle(void)
{
    if (!s_drv.init) return HB_NFC_ERR_INTERNAL;

    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    s_drv.field = false;
    hb_delay_ms(5);

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_FIELD_ON);
    hb_delay_ms(5);
    hb_spi_direct_cmd(CMD_RESET_RX_GAIN);
    s_drv.field = true;

    return HB_NFC_OK;
}

hb_nfc_err_t st25r_set_mode_nfca(void)
{
    /* REG_MODE uses a technology bitmask:
     *   bit 3 (0x08) = NFC-A   bit 4 (0x10) = NFC-B
     *   bit 5 (0x20) = NFC-F   bits 4+5 (0x30) = NFC-V
     *   bit 7 (0x80) = target  0x88 = NFC-A target
     * MODE_POLL_NFCA = 0x08 is correct for ISO14443A polling. */
    hb_spi_reg_write(REG_MODE, MODE_POLL_NFCA);
    hb_spi_reg_write(REG_BIT_RATE, 0x00);

    uint8_t iso_def;
    hb_spi_reg_read(REG_ISO14443A, &iso_def);
    iso_def &= (uint8_t)~0xC1;
    hb_spi_reg_write(REG_ISO14443A, iso_def);

    return HB_NFC_OK;
}

hb_nfc_err_t highboy_nfc_init(const highboy_nfc_config_t* config)
{
    hb_nfc_err_t err = st25r_init(config);
    if (err != HB_NFC_OK) return err;

    /* Configure default external field detector thresholds. */
    (void)hb_spi_reg_write(REG_FIELD_THRESH_ACT,   FIELD_THRESH_ACT_TRG);
    (void)hb_spi_reg_write(REG_FIELD_THRESH_DEACT, FIELD_THRESH_DEACT_TRG);

    /* Mask timer IRQs - poller never uses the NFC timer, so these would
     * only cause spurious GPIO wake-ups during st25r_irq_wait_txe. */
    (void)hb_spi_reg_write(REG_MASK_TIMER_NFC_INT, 0xFF);

    /* Try cached AAT first, otherwise run calibration. */
    st25r_aat_result_t aat = { 0 };
    if (st25r_aat_load_nvs(&aat) == HB_NFC_OK) {
        (void)hb_spi_reg_write(REG_ANT_TUNE_A, aat.dac_a);
        (void)hb_spi_reg_write(REG_ANT_TUNE_B, aat.dac_b);
        ESP_LOGI(TAG, "AAT loaded: A=0x%02X B=0x%02X", aat.dac_a, aat.dac_b);
    } else {
        err = st25r_aat_calibrate(&aat);
        if (err == HB_NFC_OK) {
            (void)st25r_aat_save_nvs(&aat);
        }
    }

    return HB_NFC_OK;
}

void highboy_nfc_deinit(void)
{
    st25r_deinit();
}

hb_nfc_err_t highboy_nfc_ping(uint8_t* chip_id)
{
    uint8_t id = 0, type = 0, rev = 0;
    hb_nfc_err_t err = st25r_check_id(&id, &type, &rev);
    if (err != HB_NFC_OK) return err;
    if (chip_id) *chip_id = id;
    return HB_NFC_OK;
}

hb_nfc_err_t highboy_nfc_field_on(void)
{
    return st25r_field_on();
}

void highboy_nfc_field_off(void)
{
    st25r_field_off();
}

uint8_t highboy_nfc_measure_amplitude(void)
{
    hb_spi_direct_cmd(CMD_MEAS_AMPLITUDE);
    hb_delay_ms(2);
    uint8_t ad = 0;
    hb_spi_reg_read(REG_AD_RESULT, &ad);
    return ad;
}

bool highboy_nfc_field_detected(uint8_t* aux_display)
{
    uint8_t aux = 0;
    if (hb_spi_reg_read(REG_AUX_DISPLAY, &aux) != HB_NFC_OK) {
        if (aux_display) *aux_display = 0;
        return false;
    }
    if (aux_display) *aux_display = aux;
    return (aux & 0x01U) != 0; /* efd_o */
}

void st25r_dump_regs(void)
{
    if (!s_drv.init) return;
    uint8_t regs[64];
    for (int i = 0; i < 64; i++) {
        hb_spi_reg_read((uint8_t)i, &regs[i]);
    }
    ESP_LOGI(TAG, "Reg Dump");
    for (int r = 0; r < 64; r += 16) {
        ESP_LOGI(TAG, "%02X: %02X %02X %02X %02X %02X %02X %02X %02X"
                       "%02X %02X %02X %02X %02X %02X %02X %02X",
                 r,
                 regs[r+0],  regs[r+1],  regs[r+2],  regs[r+3],
                 regs[r+4],  regs[r+5],  regs[r+6],  regs[r+7],
                 regs[r+8],  regs[r+9],  regs[r+10], regs[r+11],
                 regs[r+12], regs[r+13], regs[r+14], regs[r+15]);
    }
}

const char* hb_nfc_err_str(hb_nfc_err_t err)
{
    switch (err) {
    case HB_NFC_OK:             return "OK";
    case HB_NFC_ERR_SPI_INIT:   return "SPI init failed";
    case HB_NFC_ERR_SPI_XFER:   return "SPI transfer failed";
    case HB_NFC_ERR_GPIO:        return "GPIO init failed";
    case HB_NFC_ERR_TIMEOUT:     return "Timeout";
    case HB_NFC_ERR_CHIP_ID:    return "Bad chip ID";
    case HB_NFC_ERR_FIFO_OVR:   return "FIFO overflow";
    case HB_NFC_ERR_FIELD:       return "Field error";
    case HB_NFC_ERR_NO_CARD:    return "No card";
    case HB_NFC_ERR_CRC:         return "CRC error";
    case HB_NFC_ERR_COLLISION:   return "Collision";
    case HB_NFC_ERR_NACK:        return "NACK";
    case HB_NFC_ERR_AUTH:        return "Auth failed";
    case HB_NFC_ERR_PROTOCOL:    return "Protocol error";
    case HB_NFC_ERR_TX_TIMEOUT: return "TX timeout";
    case HB_NFC_ERR_PARAM:       return "Bad param";
    case HB_NFC_ERR_INTERNAL:    return "Internal error";
    default:                     return "Unknown";
    }
}
