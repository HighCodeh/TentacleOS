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
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
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

    hb_delay_us(5000);

    hb_spi_direct_cmd(CMD_SET_DEFAULT);

    hb_delay_us(2000);

    uint8_t id, type, rev;
    err = st25r_check_id(&id, &type, &rev);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "Chip ID check FAILED");
        hb_spi_deinit();
        hb_gpio_deinit();
        return err;
    }

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

hb_nfc_err_t st25r_check_id(uint8_t* id, uint8_t* type, uint8_t* rev)
{
    uint8_t val;
    hb_nfc_err_t err = hb_spi_reg_read(REG_IC_IDENTITY, &val);
    if (err != HB_NFC_OK) return HB_NFC_ERR_CHIP_ID;

    if (id)   *id   = val;
    if (type) *type = (val >> 3) & 0x1F;
    if (rev)  *rev  = val & 0x07;

    ESP_LOGI(TAG, "IC_IDENTITY: 0x%02X (type=0x%02X rev=%u)",
             val, (val >> 3) & 0x1F, val & 0x07);

    /* Sanity: type should be non-zero */
    if (val == 0x00 || val == 0xFF) {
        ESP_LOGE(TAG, "Bad IC ID 0x%02X — check SPI wiring!", val);
        return HB_NFC_ERR_CHIP_ID;
    }
    return HB_NFC_OK;
}

hb_nfc_err_t st25r_field_on(void)
{
    if (!s_drv.init) return HB_NFC_ERR_INTERNAL;
    if (s_drv.field) return HB_NFC_OK;

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_FIELD_ON);
    hb_delay_us(5000);
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
    ESP_LOGD(TAG, "RF field OFF");
}

bool st25r_field_is_on(void)
{
    return s_drv.field;
}

hb_nfc_err_t st25r_field_cycle(void)
{
    if (!s_drv.init) return HB_NFC_ERR_INTERNAL;

    /* Field OFF */
    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    s_drv.field = false;
    hb_delay_us(5000);

    /* Field ON */
    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_FIELD_ON);
    hb_delay_us(5000);
    hb_spi_direct_cmd(CMD_RESET_RX_GAIN);
    s_drv.field = true;

    return HB_NFC_OK;
}

hb_nfc_err_t st25r_set_mode_nfca(void)
{
    hb_spi_reg_write(REG_MODE, MODE_POLL_NFCA);
    hb_spi_reg_write(REG_BIT_RATE, 0x00);

    uint8_t iso_def;
    hb_spi_reg_read(REG_ISO14443A, &iso_def);
    ESP_LOGD(TAG, "ISO14443A reg default = 0x%02X", iso_def);
    iso_def &= (uint8_t)~0xC1;
    hb_spi_reg_write(REG_ISO14443A, iso_def);

    return HB_NFC_OK;
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
        ESP_LOGI(TAG, "%02X: %02X %02X %02X %02X  %02X %02X %02X %02X  "
                       "%02X %02X %02X %02X  %02X %02X %02X %02X",
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
