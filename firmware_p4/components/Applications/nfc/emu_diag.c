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
#include "emu_diag.h"
#include "mf_classic_emu.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "emu_diag";

static void dump_key_regs(const char* label)
{
    uint8_t r[64];
    for (int i = 0; i < 64; i++) {
        hb_spi_reg_read((uint8_t)i, &r[i]);
    }

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "REG DUMP: %s", label);
    ESP_LOGW(TAG, "IO_CONF1(00)=%02X IO_CONF2(01)=%02X", r[0x00], r[0x01]);
    ESP_LOGW(TAG, "OP_CTRL(02)=%02X [EN=%d RX_EN=%d TX_EN=%d wu=%d]",
             r[0x02],
             (r[0x02] >> 7) & 1,
             (r[0x02] >> 6) & 1,
             (r[0x02] >> 3) & 1,
             (r[0x02] >> 2) & 1);
    ESP_LOGW(TAG, "MODE(03)=%02X [targ=%d om=0x%X]",
             r[0x03],
             (r[0x03] >> 7) & 1,
             (r[0x03] >> 3) & 0x0F);
    ESP_LOGW(TAG, "BIT_RATE(04)=%02X ISO14443A(05)=%02X [no_tx_par=%d no_rx_par=%d antcl=%d]",
             r[0x04], r[0x05],
             (r[0x05] >> 7) & 1, (r[0x05] >> 6) & 1, r[0x05] & 1);
    ESP_LOGW(TAG, "PASSIVE_TGT(08)=%02X [d_106=%d d_212=%d d_ap2p=%d]",
             r[0x08], r[0x08] & 1, (r[0x08] >> 1) & 1, (r[0x08] >> 2) & 1);
    ESP_LOGW(TAG, "AUX_DEF(0A)=%02X", r[0x0A]);
    ESP_LOGW(TAG, "RX_CONF: %02X %02X %02X %02X",
             r[0x0B], r[0x0C], r[0x0D], r[0x0E]);
    ESP_LOGW(TAG, "MASK: MAIN(16)=%02X TMR(17)=%02X ERR(18)=%02X TGT(19)=%02X",
             r[0x16], r[0x17], r[0x18], r[0x19]);
    ESP_LOGW(TAG, "IRQ: MAIN(1A)=%02X TMR(1B)=%02X ERR(1C)=%02X TGT(1D)=%02X",
             r[0x1A], r[0x1B], r[0x1C], r[0x1D]);
    ESP_LOGW(TAG, "PT_STS(21)=%02X", r[0x21]);
    ESP_LOGW(TAG, "AD_RESULT(24)=%d ANT_TUNE: A=%02X B=%02X",
             r[0x24], r[0x26], r[0x27]);
    ESP_LOGW(TAG, "TX_DRIVER(28)=%02X PT_MOD(29)=%02X",
             r[0x28], r[0x29]);
    ESP_LOGW(TAG, "FLD_ACT(2A)=%02X FLD_DEACT(2B)=%02X",
             r[0x2A], r[0x2B]);
    ESP_LOGW(TAG, "REG_CTRL(2C)=%02X RSSI(2D)=%02X",
             r[0x2C], r[0x2D]);
    ESP_LOGW(TAG, "AUX_DISP(31)=%02X [efd_o=%d efd_i=%d osc=%d nfc_t=%d rx_on=%d rx_act=%d tx_on=%d tgt=%d]",
             r[0x31],
             (r[0x31] >> 0) & 1,
             (r[0x31] >> 1) & 1,
             (r[0x31] >> 2) & 1,
             (r[0x31] >> 3) & 1,
             (r[0x31] >> 4) & 1,
             (r[0x31] >> 5) & 1,
             (r[0x31] >> 6) & 1,
             (r[0x31] >> 7) & 1);
    ESP_LOGW(TAG, "IC_ID(3F)=%02X [type=%d rev=%d]",
             r[0x3F], (r[0x3F] >> 3) & 0x1F, r[0x3F] & 0x07);
    ESP_LOGW(TAG, "");
}

static void read_regs(uint8_t r[64])
{
    for (int i = 0; i < 64; i++) {
        hb_spi_reg_read((uint8_t)i, &r[i]);
    }
}

typedef struct {
    uint8_t addr;
    const char* name;
} reg_name_t;

static void diff_key_regs(const char* a_label, const uint8_t* a,
                          const char* b_label, const uint8_t* b)
{
    static const reg_name_t keys[] = {
        { REG_IO_CONF1, "IO_CONF1" },
        { REG_IO_CONF2, "IO_CONF2" },
        { REG_OP_CTRL, "OP_CTRL" },
        { REG_MODE, "MODE" },
        { REG_BIT_RATE, "BIT_RATE" },
        { REG_ISO14443A, "ISO14443A" },
        { REG_PASSIVE_TARGET, "PASSIVE_TGT" },
        { REG_AUX_DEF, "AUX_DEF" },
        { REG_RX_CONF1, "RX_CONF1" },
        { REG_RX_CONF2, "RX_CONF2" },
        { REG_RX_CONF3, "RX_CONF3" },
        { REG_RX_CONF4, "RX_CONF4" },
        { REG_MASK_MAIN_INT, "MASK_MAIN" },
        { REG_MASK_TIMER_NFC_INT, "MASK_TMR" },
        { REG_MASK_ERROR_WUP_INT, "MASK_ERR" },
        { REG_MASK_TARGET_INT, "MASK_TGT" },
        { REG_PASSIVE_TARGET_STS, "PT_STS" },
        { REG_AD_RESULT, "AD_RESULT" },
        { REG_ANT_TUNE_A, "ANT_TUNE_A" },
        { REG_ANT_TUNE_B, "ANT_TUNE_B" },
        { REG_TX_DRIVER, "TX_DRIVER" },
        { REG_PT_MOD, "PT_MOD" },
        { REG_FIELD_THRESH_ACT, "FLD_ACT" },
        { REG_FIELD_THRESH_DEACT, "FLD_DEACT" },
        { REG_REGULATOR_CTRL, "REG_CTRL" },
        { REG_RSSI_RESULT, "RSSI" },
        { REG_AUX_DISPLAY, "AUX_DISP" },
    };

    bool key_map[64] = { 0 };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        key_map[keys[i].addr] = true;
    }

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "REG DIFF: %s vs %s (KEY REGS)", a_label, b_label);
    int diff_count = 0;
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        uint8_t addr = keys[i].addr;
        if (a[addr] != b[addr]) {
            ESP_LOGW(TAG, "%-12s (0x%02X): %s=0x%02X %s=0x%02X",
                     keys[i].name, addr, a_label, a[addr], b_label, b[addr]);
            diff_count++;
        }
    }
    if (diff_count == 0) {
        ESP_LOGW(TAG, "(no differences in key registers)");
    }

    int other_diff = 0;
    for (int i = 0; i < 64; i++) {
        if (!key_map[i] && a[i] != b[i]) other_diff++;
    }
    if (other_diff > 0) {
        ESP_LOGW(TAG, "+ %d differences in other registers", other_diff);
    }
    ESP_LOGW(TAG, "");
}

static uint8_t measure_field(void)
{
    hb_spi_direct_cmd(CMD_MEAS_AMPLITUDE);
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t ad = 0;
    hb_spi_reg_read(REG_AD_RESULT, &ad);
    return ad;
}

static uint8_t read_aux(void)
{
    uint8_t aux = 0;
    hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
    return aux;
}

static void test_field_detection(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST 1: FIELD DETECTION");
    ESP_LOGW(TAG, "PRESENT THE PHONE/READER NOW!");
    ESP_LOGW(TAG, "");

    hb_spi_reg_write(REG_OP_CTRL, 0x80);
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t ad_en = measure_field();
    uint8_t aux_en = read_aux();
    ESP_LOGW(TAG, "[A] OP=0x80 (EN) AD=%3d AUX=0x%02X [osc=%d] %s",
             ad_en, aux_en, (aux_en >> 2) & 1, ad_en > 5 ? "OK" : "FAIL");

    hb_spi_reg_write(REG_OP_CTRL, 0xC0);
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t ad_rx = measure_field();
    uint8_t aux_rx = read_aux();
    ESP_LOGW(TAG, "[B] OP=0xC0 (EN+RX) AD=%3d AUX=0x%02X [osc=%d] %s",
             ad_rx, aux_rx, (aux_rx >> 2) & 1, ad_rx > 5 ? "OK" : "FAIL");

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "Continuous read for 5s:");
    uint8_t max_ad = 0;
    for (int i = 0; i < 50; i++) {
        uint8_t ad = measure_field();
        if (ad > max_ad) max_ad = ad;
        if ((i % 10) == 0) {
            uint8_t a = read_aux();
            ESP_LOGW(TAG, "t=%dms: AD=%3d AUX=0x%02X [efd=%d osc=%d]",
                     i * 100, ad, a, a & 1, (a >> 2) & 1);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGW(TAG, "AD max: %d", max_ad);
    if (max_ad < 5) {
        ESP_LOGE(TAG, "NO EXTERNAL FIELD DETECTED!");
        ESP_LOGE(TAG, "Check if the NFC reader is on and close");
        ESP_LOGE(TAG, "The antenna may not capture external field (TX only)");
    }
    ESP_LOGW(TAG, "");
}

static bool test_target_config(int cfg_num, uint8_t op_ctrl,
                                uint8_t fld_act, uint8_t fld_deact,
                                uint8_t pt_mod, uint8_t passive_target,
                                bool antcl)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST 2.%d: CONFIG (OP=0x%02X PT=0x%02X ANTCL=%d ACT=0x%02X DEACT=0x%02X MOD=0x%02X)",
             cfg_num, op_ctrl, passive_target, antcl ? 1 : 0, fld_act, fld_deact, pt_mod);

    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    vTaskDelay(pdMS_TO_TICKS(2));
    hb_spi_direct_cmd(CMD_SET_DEFAULT);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t ic = 0;
    hb_spi_reg_read(REG_IC_IDENTITY, &ic);
    if (ic == 0x00 || ic == 0xFF) {
        ESP_LOGE(TAG, "CHIP NOT RESPONDING! IC=0x%02X", ic);
        ESP_LOGW(TAG, "");
        return false;
    }

    hb_spi_reg_write(REG_OP_CTRL, 0x80);
    bool osc = false;
    for (int i = 0; i < 100; i++) {
        uint8_t aux = 0;
        hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
        if (aux & 0x04) { osc = true; break; }
        vTaskDelay(1);
    }
    ESP_LOGW(TAG, "Oscillator: %s", osc ? "OK" : "NOT STABLE");

    hb_spi_direct_cmd(CMD_ADJUST_REGULATORS);
    vTaskDelay(pdMS_TO_TICKS(5));

    hb_spi_reg_write(REG_MODE, 0x88);
    hb_spi_reg_write(REG_BIT_RATE, 0x00);
    uint8_t iso14443a = antcl ? ISO14443A_ANTCL : 0x00;
    hb_spi_reg_write(REG_ISO14443A, iso14443a);
    hb_spi_reg_write(REG_PASSIVE_TARGET, passive_target);

    hb_spi_reg_write(REG_FIELD_THRESH_ACT, fld_act);
    hb_spi_reg_write(REG_FIELD_THRESH_DEACT, fld_deact);
    hb_spi_reg_write(REG_PT_MOD, pt_mod);

    mfc_emu_load_pt_memory();

    hb_spi_reg_write(REG_MASK_MAIN_INT, 0x00);
    hb_spi_reg_write(REG_MASK_TIMER_NFC_INT, 0x00);
    hb_spi_reg_write(REG_MASK_ERROR_WUP_INT, 0x00);
    hb_spi_reg_write(REG_MASK_TARGET_INT, 0x00);

    st25r_irq_read();

    hb_spi_reg_write(REG_OP_CTRL, op_ctrl);
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t mode_rb = 0, op_rb = 0;
    hb_spi_reg_read(REG_MODE, &mode_rb);
    hb_spi_reg_read(REG_OP_CTRL, &op_rb);
    uint8_t aux0 = read_aux();
    ESP_LOGW(TAG, "Pre-SENSE: MODE=0x%02X OP=0x%02X AUX=0x%02X [osc=%d efd=%d]",
             mode_rb, op_rb, aux0, (aux0 >> 2) & 1, aux0 & 1);

    hb_spi_direct_cmd(CMD_GOTO_SENSE);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t pt_sts = 0;
    hb_spi_reg_read(REG_PASSIVE_TARGET_STS, &pt_sts);
    uint8_t aux1 = read_aux();
    ESP_LOGW(TAG, "Pos-SENSE: PT_STS=0x%02X AUX=0x%02X [osc=%d efd=%d tgt=%d]",
             pt_sts, aux1, (aux1 >> 2) & 1, aux1 & 1, (aux1 >> 7) & 1);

    ESP_LOGW(TAG, "Monitoring 10s (READER CLOSE!)...");

    bool wu_a = false, sdd_c = false, any_irq = false;
    int64_t t0 = esp_timer_get_time();
    int last_report = -1;

    while ((esp_timer_get_time() - t0) < 10000000LL) {
        uint8_t tgt_irq = 0, main_irq = 0, err_irq = 0;
        hb_spi_reg_read(REG_TARGET_INT, &tgt_irq);
        hb_spi_reg_read(REG_MAIN_INT, &main_irq);
        hb_spi_reg_read(REG_ERROR_INT, &err_irq);

        if (tgt_irq || main_irq || err_irq) {
            int ms = (int)((esp_timer_get_time() - t0) / 1000);
            ESP_LOGW(TAG, "[%dms] TGT=0x%02X MAIN=0x%02X ERR=0x%02X",
                     ms, tgt_irq, main_irq, err_irq);
            any_irq = true;
            if (tgt_irq & 0x80) { ESP_LOGI(TAG, "WU_A!"); wu_a = true; }
            if (tgt_irq & 0x40) { ESP_LOGI(TAG, "WU_A_X (anti-col done)!"); }
            if (tgt_irq & 0x04) { ESP_LOGI(TAG, "SDD_C (SELECTED)!"); sdd_c = true; }
            if (tgt_irq & 0x08) { ESP_LOGI(TAG, "OSCF (osc stable)"); }
            if (main_irq & 0x04) {
                ESP_LOGI(TAG, "RXE (data received)!");
                uint8_t fs1 = 0;
                hb_spi_reg_read(REG_FIFO_STATUS1, &fs1);
                ESP_LOGI(TAG, "FIFO: %d bytes", fs1);
            }
        }

        int sec = (int)((esp_timer_get_time() - t0) / 1000000);
        if (sec != last_report && (sec % 3) == 0) {
            last_report = sec;
            uint8_t ad = measure_field();
            uint8_t a = read_aux();
            hb_spi_direct_cmd(CMD_GOTO_SENSE);
            ESP_LOGW(TAG, "[%ds] AD=%d AUX=0x%02X [efd=%d osc=%d]",
                     sec, ad, a, a & 1, (a >> 2) & 1);
        }

        vTaskDelay(1);
    }

    ESP_LOGW(TAG, "");
    if (sdd_c) {
        ESP_LOGI(TAG, "CONFIG %d: SUCCESS SELECTED!", cfg_num);
    } else if (wu_a) {
        ESP_LOGI(TAG, "CONFIG %d: Field detected, anti-col failed", cfg_num);
    } else if (any_irq) {
        ESP_LOGW(TAG, "CONFIG %d: IRQ seen but no WU_A", cfg_num);
    } else {
        ESP_LOGE(TAG, "CONFIG %d: NO IRQ in 10s", cfg_num);
    }
    ESP_LOGW(TAG, "");

    return sdd_c;
}

static void test_pt_memory(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST 3: PT MEMORY");

    (void)hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_EN);
    vTaskDelay(pdMS_TO_TICKS(2));

    hb_nfc_err_t err = mfc_emu_load_pt_memory();
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "mfc_emu_load_pt_memory failed: %d", err);
        ESP_LOGW(TAG, "");
        hb_spi_reg_write(REG_OP_CTRL, 0x00);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t ptm[15] = {0};
    err = hb_spi_pt_mem_read(ptm, 15);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "hb_spi_pt_mem_read failed: %d", err);
        ESP_LOGW(TAG, "");
        hb_spi_reg_write(REG_OP_CTRL, 0x00);
        return;
    }

    ESP_LOGW(TAG, "PT Memory: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             ptm[0], ptm[1], ptm[2], ptm[3], ptm[4], ptm[5], ptm[6], ptm[7],
             ptm[8], ptm[9], ptm[10], ptm[11], ptm[12], ptm[13], ptm[14]);
    ESP_LOGW(TAG, "ATQA=%02X%02X UID=%02X%02X%02X%02X BCC=%02X(calc:%02X) SAK=%02X",
             ptm[0], ptm[1], ptm[2], ptm[3], ptm[4], ptm[5],
             ptm[6], ptm[2] ^ ptm[3] ^ ptm[4] ^ ptm[5], ptm[7]);

    bool bcc_ok = (ptm[6] == (ptm[2] ^ ptm[3] ^ ptm[4] ^ ptm[5]));
    ESP_LOGW(TAG, "BCC: %s", bcc_ok ? "OK" : "WRONG!");

    uint8_t test[15] = {0x04, 0x00, 0xDE, 0xAD, 0xBE, 0xEF,
                         0xDE ^ 0xAD ^ 0xBE ^ 0xEF, 0x08, 0,0,0,0,0,0,0};
    hb_spi_pt_mem_write(SPI_PT_MEM_A_WRITE, test, 15);
    vTaskDelay(1);
    uint8_t rb[15] = {0};
    err = hb_spi_pt_mem_read(rb, 15);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "hb_spi_pt_mem_read (rb) failed: %d", err);
        ESP_LOGW(TAG, "");
        hb_spi_reg_write(REG_OP_CTRL, 0x00);
        return;
    }
    bool match = (memcmp(test, rb, 15) == 0);
    ESP_LOGW(TAG, "Write/Read test: %s", match ? "OK" : "FAILED!");
    if (!match) {
        ESP_LOGW(TAG, "Written: %02X %02X %02X %02X %02X %02X %02X %02X",
                 test[0],test[1],test[2],test[3],test[4],test[5],test[6],test[7]);
        ESP_LOGW(TAG, "Read: %02X %02X %02X %02X %02X %02X %02X %02X",
                 rb[0],rb[1],rb[2],rb[3],rb[4],rb[5],rb[6],rb[7]);
    }

    mfc_emu_load_pt_memory();
    ESP_LOGW(TAG, "");
}

static void test_oscillator(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST 4: OSCILLATOR");

    uint8_t aux = 0, main = 0, tgt = 0;
    if (hb_spi_reg_read(REG_AUX_DISPLAY, &aux) != HB_NFC_OK ||
        hb_spi_reg_read(REG_MAIN_INT, &main) != HB_NFC_OK ||
        hb_spi_reg_read(REG_TARGET_INT, &tgt) != HB_NFC_OK) {
        ESP_LOGW(TAG, "SPI error reading AUX/MAIN/TGT, skipping oscillator test");
        ESP_LOGW(TAG, "");
        return;
    }
    ESP_LOGW(TAG, "AUX_DISPLAY=0x%02X (before enabling EN)", aux);
    ESP_LOGW(TAG, "osc_ok=%d efd_o=%d rx_on=%d tgt=%d",
             (aux>>2)&1, (aux>>0)&1, (aux>>4)&1, (aux>>7)&1);

    if (hb_spi_reg_write(REG_OP_CTRL, 0x80) != HB_NFC_OK) {
        ESP_LOGW(TAG, "SPI error writing OP_CTRL, skipping oscillator test");
        ESP_LOGW(TAG, "");
        return;
    }
    ESP_LOGW(TAG, "OP_CTRL=0x80 (EN) enabling oscillator...");

    bool osc_started = false;
    for (int i = 0; i < 100; i++) {
        uint8_t a = 0, m = 0, t = 0;
        if (hb_spi_reg_read(REG_AUX_DISPLAY, &a) != HB_NFC_OK ||
            hb_spi_reg_read(REG_MAIN_INT, &m) != HB_NFC_OK ||
            hb_spi_reg_read(REG_TARGET_INT, &t) != HB_NFC_OK) {
            ESP_LOGW(TAG, "SPI error reading AUX/MAIN/TGT, aborting test");
            ESP_LOGW(TAG, "");
            return;
        }
        if ((a & 0x04) || (m & IRQ_MAIN_OSC) || (t & IRQ_TGT_OSCF)) {
            ESP_LOGI(TAG, "Oscillator stable at %dms! AUX=0x%02X MAIN=0x%02X TGT=0x%02X",
                     i * 10, a, m, t);
            osc_started = true;
            break;
        }
        vTaskDelay(1);
    }

    if (!osc_started) {
        if (hb_spi_reg_read(REG_AUX_DISPLAY, &aux) != HB_NFC_OK ||
            hb_spi_reg_read(REG_MAIN_INT, &main) != HB_NFC_OK ||
            hb_spi_reg_read(REG_TARGET_INT, &tgt) != HB_NFC_OK) {
            ESP_LOGW(TAG, "SPI error reading AUX/MAIN/TGT, aborting test");
            ESP_LOGW(TAG, "");
            return;
        }
        ESP_LOGE(TAG, "Oscillator NOT started after 1s. AUX=0x%02X MAIN=0x%02X TGT=0x%02X",
                 aux, main, tgt);
        ESP_LOGE(TAG, "Bits: efd_o=%d efd_i=%d osc=%d nfc_t=%d rx_on=%d",
                 (aux>>0)&1, (aux>>1)&1, (aux>>2)&1, (aux>>3)&1, (aux>>4)&1);
    }

    if (hb_spi_direct_cmd(CMD_ADJUST_REGULATORS) != HB_NFC_OK) {
        ESP_LOGW(TAG, "SPI error on CMD_ADJUST_REGULATORS, ending test");
        ESP_LOGW(TAG, "");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t rc = 0;
    if (hb_spi_reg_read(REG_REGULATOR_CTRL, &rc) != HB_NFC_OK ||
        hb_spi_reg_read(REG_AUX_DISPLAY, &aux) != HB_NFC_OK) {
        ESP_LOGW(TAG, "SPI error reading REG_CTRL/AUX, ending test");
        ESP_LOGW(TAG, "");
        return;
    }
    ESP_LOGW(TAG, "After calibration: AUX=0x%02X REG_CTRL=0x%02X", aux, rc);
    ESP_LOGW(TAG, "");
}

static void test_aux_def_sweep(void)
{
    static const uint8_t vals[] = {
        0x00, 0x01, 0x02, 0x04, 0x08,
        0x10, 0x20, 0x40, 0x80, 0xFF
    };

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST X: AUX_DEF SWEEP");
    ESP_LOGW(TAG, "(look for AD>0 or efd=1 with reader close)");

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_EN | OP_CTRL_RX_EN);
    vTaskDelay(pdMS_TO_TICKS(5));

    for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        uint8_t v = vals[i];
        hb_spi_reg_write(REG_AUX_DEF, v);
        vTaskDelay(1);
        hb_spi_direct_cmd(CMD_MEAS_AMPLITUDE);
        vTaskDelay(2);
        uint8_t ad = 0;
        hb_spi_reg_read(REG_AD_RESULT, &ad);
        uint8_t aux = read_aux();
        ESP_LOGW(TAG, "AUX_DEF=0x%02X -> AD=%3u AUX=0x%02X [efd_o=%d efd_i=%d]",
                 v, ad, aux, aux & 1, (aux >> 1) & 1);
    }

    hb_spi_reg_write(REG_AUX_DEF, 0x00);
    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    ESP_LOGW(TAG, "");
}

static void test_self_field_measure(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST S: SELF FIELD MEASURE");
    ESP_LOGW(TAG, "(measures AD/RSSI with self field enabled)");

    st25r_field_on();
    vTaskDelay(pdMS_TO_TICKS(5));
    hb_spi_direct_cmd(CMD_RESET_RX_GAIN);
    vTaskDelay(pdMS_TO_TICKS(1));
    hb_spi_direct_cmd(CMD_MEAS_AMPLITUDE);
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t ad = 0, rssi = 0, aux = 0;
    hb_spi_reg_read(REG_AD_RESULT, &ad);
    hb_spi_reg_read(REG_RSSI_RESULT, &rssi);
    hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
    ESP_LOGW(TAG, "self: AD=%u RSSI=0x%02X AUX=0x%02X [efd=%d osc=%d]",
             ad, rssi, aux, aux & 1, (aux >> 2) & 1);

    st25r_field_off();
    ESP_LOGW(TAG, "");
}

static void test_poller_vs_target_regs(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST 0: DIFF POLLER vs TARGET");

    static uint8_t poller[64];
    static uint8_t target[64];
    static mfc_emu_card_data_t emu;
    memset(poller, 0, sizeof(poller));
    memset(target, 0, sizeof(target));
    memset(&emu, 0, sizeof(emu));

    st25r_set_mode_nfca();
    st25r_field_on();
    vTaskDelay(pdMS_TO_TICKS(5));
    read_regs(poller);
    st25r_field_off();

    emu.uid_len = 4;
    emu.uid[0] = 0x04; emu.uid[1] = 0x11; emu.uid[2] = 0x22; emu.uid[3] = 0x33;
    emu.atqa[0] = 0x44; emu.atqa[1] = 0x00;
    emu.sak = 0x04;
    emu.type = MF_CLASSIC_1K;
    emu.sector_count = 0;
    emu.total_blocks = 0;

    (void)mfc_emu_init(&emu);
    (void)mfc_emu_configure_target();
    read_regs(target);

    diff_key_regs("POLL", poller, "TGT", target);
    ESP_LOGW(TAG, "");
}

static void test_rssi_monitor(int seconds)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST Y: RSSI MONITOR (%ds)", seconds);
    ESP_LOGW(TAG, "(NFC reader should be touching)");

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_EN | OP_CTRL_RX_EN);
    vTaskDelay(pdMS_TO_TICKS(5));
    hb_spi_direct_cmd(CMD_CLEAR_RSSI);
    vTaskDelay(pdMS_TO_TICKS(2));

    for (int i = 0; i < seconds; i++) {
        uint8_t rssi = 0, aux = 0;
        hb_spi_reg_read(REG_RSSI_RESULT, &rssi);
        hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
        ESP_LOGW(TAG, "t=%ds RSSI=0x%02X AUX=0x%02X [efd=%d osc=%d]",
                 i, rssi, aux, aux & 1, (aux >> 2) & 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    ESP_LOGW(TAG, "");
}

static void test_rssi_fast(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST Y2: RSSI FAST (2s @5ms)");
    ESP_LOGW(TAG, "(NFC reader should be touching)");

    (void)hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_EN | OP_CTRL_RX_EN);
    vTaskDelay(pdMS_TO_TICKS(5));
    (void)hb_spi_direct_cmd(CMD_CLEAR_RSSI);
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t max_rssi = 0;
    uint8_t max_ad = 0;
    int64_t t0 = esp_timer_get_time();
    const int step_ms = 5;
    const int total_ms = 2000;
    const int samples = total_ms / step_ms;
    bool spi_ok = true;

    for (int i = 0; i < samples; i++) {
        uint8_t rssi = 0;
        if (hb_spi_reg_read(REG_RSSI_RESULT, &rssi) != HB_NFC_OK) {
            ESP_LOGW(TAG, "SPI error reading RSSI (i=%d)", i);
            spi_ok = false;
            break;
        }
        if (rssi > max_rssi) max_rssi = rssi;

        if ((i % 10) == 0) {
            uint8_t aux = 0, ad = 0;
            (void)hb_spi_direct_cmd(CMD_MEAS_AMPLITUDE);
            vTaskDelay(1);
            if (hb_spi_reg_read(REG_AD_RESULT, &ad) != HB_NFC_OK ||
                hb_spi_reg_read(REG_AUX_DISPLAY, &aux) != HB_NFC_OK) {
                ESP_LOGW(TAG, "SPI error reading AD/AUX (i=%d)", i);
                spi_ok = false;
                break;
            }
            if (ad > max_ad) max_ad = ad;

            int ms = (int)((esp_timer_get_time() - t0) / 1000);
            ESP_LOGW(TAG, "t=%dms RSSI=0x%02X AD=%u AUX=0x%02X [efd=%d osc=%d]",
                     ms, rssi, ad, aux, aux & 1, (aux >> 2) & 1);
        }

        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }

    ESP_LOGW(TAG, "max RSSI=0x%02X max AD=%u", max_rssi, max_ad);
    if (spi_ok) {
        (void)hb_spi_reg_write(REG_OP_CTRL, 0x00);
    }
    ESP_LOGW(TAG, "");
}

static void test_rf_collision(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "TEST Z: RF COLLISION (NFC INITIAL)");
    ESP_LOGW(TAG, "(reader NFC touching, waiting IRQ)");

    hb_spi_reg_write(REG_MODE, MODE_TARGET_NFCA);
    hb_spi_reg_write(REG_BIT_RATE, 0x00);
    hb_spi_reg_write(REG_ISO14443A, 0x00);
    hb_spi_reg_write(REG_PASSIVE_TARGET, 0x00);
    hb_spi_reg_write(REG_FIELD_THRESH_ACT, 0x33);
    hb_spi_reg_write(REG_FIELD_THRESH_DEACT, 0x22);
    hb_spi_reg_write(REG_PT_MOD, 0x60);
    mfc_emu_load_pt_memory();
    hb_spi_reg_write(REG_OP_CTRL, 0x00);

    hb_spi_reg_write(REG_MASK_MAIN_INT, 0x00);
    hb_spi_reg_write(REG_MASK_TIMER_NFC_INT, 0x00);
    hb_spi_reg_write(REG_MASK_ERROR_WUP_INT, 0x00);
    hb_spi_reg_write(REG_MASK_TARGET_INT, 0x00);
    st25r_irq_read();

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_EN | OP_CTRL_RX_EN);
    vTaskDelay(pdMS_TO_TICKS(5));

    hb_spi_direct_cmd(CMD_NFC_INITIAL_RF_COL);
    vTaskDelay(pdMS_TO_TICKS(2));
    hb_spi_direct_cmd(CMD_GOTO_SENSE);

    int64_t t0 = esp_timer_get_time();
    while ((esp_timer_get_time() - t0) < 5000000LL) {
        uint8_t tgt = 0, main = 0, err = 0;
        hb_spi_reg_read(REG_TARGET_INT, &tgt);
        hb_spi_reg_read(REG_MAIN_INT, &main);
        hb_spi_reg_read(REG_ERROR_INT, &err);
        if (tgt || main || err) {
            int ms = (int)((esp_timer_get_time() - t0) / 1000);
            ESP_LOGW(TAG, "[%dms] TGT=0x%02X MAIN=0x%02X ERR=0x%02X",
                     ms, tgt, main, err);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    ESP_LOGW(TAG, "");
}

hb_nfc_err_t emu_diag_full(void)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "EMULATION DIAGNOSTIC v2");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "KEEP THE NFC READER CLOSE DURING");
    ESP_LOGW(TAG, "THE ENTIRE DIAGNOSTIC (~60s total)");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "Waiting 5s... Present the NFC reader now!");
    vTaskDelay(pdMS_TO_TICKS(5000));

    dump_key_regs("INITIAL STATE");

    test_poller_vs_target_regs();

    test_oscillator();

    test_aux_def_sweep();

    test_self_field_measure();

    test_rssi_monitor(5);
    test_rssi_fast();

    test_pt_memory();

    test_field_detection();

    test_rf_collision();

    bool ok1 = test_target_config(1, 0xC0, 0x33, 0x22, 0x60, 0x00, false);
    if (ok1) goto done;

    bool ok2 = test_target_config(2, 0xC4, 0x33, 0x22, 0x60, 0x00, false);
    if (ok2) goto done;

    bool ok3 = test_target_config(3, 0xC8, 0x33, 0x22, 0x60, 0x00, false);
    if (ok3) goto done;

    bool ok4 = test_target_config(4, 0xCC, 0x33, 0x22, 0x60, 0x00, true);
    if (ok4) goto done;

    test_target_config(5, 0xC0, 0x03, 0x01, 0x17, 0x00, true);

done:
    dump_key_regs("FINAL STATE");

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "DIAGNOSTIC COMPLETE");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "If AD is always 0:");
    ESP_LOGW(TAG, "Antenna does not pick up external field");
    ESP_LOGW(TAG, "Needs matching circuit for passive RX");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "If AD > 0 but no WU_A:");
    ESP_LOGW(TAG, "GOTO_SENSE is not activating correctly");
    ESP_LOGW(TAG, "Or thresholds need to be adjusted");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "If WU_A OK but no SDD_C:");
    ESP_LOGW(TAG, "PT Memory or anti-collision has a problem");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "COPY THE FULL SERIAL OUTPUT AND SHARE IT!");
    ESP_LOGW(TAG, "");

    return HB_NFC_OK;
}

void emu_diag_monitor(int seconds)
{
    ESP_LOGW(TAG, "Monitor %ds...", seconds);
    int64_t t0 = esp_timer_get_time();
    while ((esp_timer_get_time() - t0) < (int64_t)seconds * 1000000LL) {
        uint8_t tgt = 0, mi = 0, ei = 0;
        hb_spi_reg_read(REG_TARGET_INT, &tgt);
        hb_spi_reg_read(REG_MAIN_INT, &mi);
        hb_spi_reg_read(REG_ERROR_INT, &ei);
        if (tgt || mi || ei) {
            ESP_LOGW(TAG, "[%dms] TGT=0x%02X MAIN=0x%02X ERR=0x%02X",
                     (int)((esp_timer_get_time() - t0) / 1000), tgt, mi, ei);
        }
        vTaskDelay(1);
    }
}
