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
 * @file felica_emu.c — DIAGNOSTIC BUILD
 * Identical to BUG-14 fix but with verbose logging in SENSE to identify
 * exactly which register/bit is triggering the premature SLEEP.
 */
#include "felica_emu.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"

static const char* TAG = "felica_emu";

#define MODE_TARGET_NFCF    0xA0U
#define OP_CTRL_TARGET      0xC3U
#define FELICA_AUX_DEF      0x18U
#define FELICA_ISO_F_CTRL   0x80U
#define FELICA_BITRATE_212  0x11U  /* tx=212, rx=212 */
#define SENSE_IDLE_TIMEOUT   30000U
#define ACTIVE_IDLE_TIMEOUT  20000U
#define TIMER_I_EON          0x10U
#define TIMER_I_EOF          0x08U

typedef enum {
    FELICA_STATE_SLEEP,
    FELICA_STATE_SENSE,
    FELICA_STATE_ACTIVE,
} felica_emu_state_t;

static felica_emu_card_t  s_card;
static felica_emu_state_t s_state      = FELICA_STATE_SLEEP;
static bool               s_init_done  = false;
static uint32_t           s_idle       = 0;

static bool wait_oscillator(int timeout_ms)
{
    for (int i = 0; i < timeout_ms; i++) {
        uint8_t aux = 0, mi = 0, ti = 0;
        hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
        hb_spi_reg_read(REG_MAIN_INT,    &mi);
        hb_spi_reg_read(REG_TARGET_INT,  &ti);
        if (((aux & 0x14U) != 0U) || ((mi & 0x80U) != 0U) || ((ti & 0x08U) != 0U)) {
            ESP_LOGI(TAG, "Osc OK in %dms: AUX=0x%02X MAIN=0x%02X TGT=0x%02X",
                     i, aux, mi, ti);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(TAG, "Osc timeout - continuing");
    return false;
}

static void tx_response(const uint8_t* data, int len)
{
    if (!data || len <= 0) return;
    st25r_fifo_clear();
    st25r_set_tx_bytes((uint16_t)len, 0);
    st25r_fifo_load(data, (size_t)len);
    hb_spi_direct_cmd(CMD_TX_WITH_CRC);
}

static void goto_sleep(void)
{
    hb_spi_direct_cmd(CMD_GOTO_SLEEP);
    s_state = FELICA_STATE_SLEEP;
    s_idle  = 0;
}

static void write_pt_mem_f(void)
{
    uint8_t pt[19] = {0};
    pt[0] = (uint8_t)(s_card.system_code >> 8);
    pt[1] = (uint8_t)(s_card.system_code & 0xFF);
    pt[2] = 0x01U; /* SENSF_RES code */
    memcpy(&pt[3], s_card.idm, FELICA_IDM_LEN);
    memcpy(&pt[11], s_card.pmm, FELICA_PMM_LEN);
    hb_spi_pt_mem_write(SPI_PT_MEM_F_WRITE, pt, sizeof(pt));
    char idm_s[24];
    snprintf(idm_s, sizeof(idm_s), "%02X%02X%02X%02X%02X%02X%02X%02X",
             s_card.idm[0], s_card.idm[1], s_card.idm[2], s_card.idm[3],
             s_card.idm[4], s_card.idm[5], s_card.idm[6], s_card.idm[7]);
    ESP_LOGI(TAG, "PT Mem F: SC=0x%04X IDm=%s PMm=%02X%02X%02X%02X%02X%02X%02X%02X",
             s_card.system_code, idm_s,
             s_card.pmm[0], s_card.pmm[1], s_card.pmm[2], s_card.pmm[3],
             s_card.pmm[4], s_card.pmm[5], s_card.pmm[6], s_card.pmm[7]);
}
static void handle_read(const uint8_t* frame, int len)
{
    if (len < 16) { ESP_LOGW(TAG, "Read: too short (%d)", len); return; }
    if (memcmp(&frame[2], s_card.idm, FELICA_IDM_LEN) != 0) {
        ESP_LOGW(TAG, "Read: IDm mismatch"); return;
    }
    uint8_t sc_count = frame[10];
    int     blk_pos  = 11 + sc_count * 2;
    if (blk_pos >= len) { ESP_LOGW(TAG, "Read: short (sc=%d)", sc_count); return; }
    uint8_t blk_count = frame[blk_pos];
    int     desc_pos  = blk_pos + 1;
    if (blk_count == 0 || blk_count > 4) {
        ESP_LOGW(TAG, "Read: bad blk_count=%d", blk_count); return;
    }
    static uint8_t resp[4 + FELICA_IDM_LEN + 2 + 1 + FELICA_EMU_MEM_SIZE];
    int rp = 0;
    resp[rp++] = 0x00U;
    resp[rp++] = FELICA_CMD_READ_RESP;
    memcpy(&resp[rp], s_card.idm, FELICA_IDM_LEN); rp += FELICA_IDM_LEN;
    resp[rp++] = 0x00U;
    resp[rp++] = 0x00U;
    resp[rp++] = blk_count;
    for (int i = 0; i < blk_count; i++) {
        if (desc_pos + 1 >= len) { ESP_LOGW(TAG, "Read: desc %d OOF", i); break; }
        uint8_t blk_flag = frame[desc_pos];
        uint8_t blk_num;
        int     desc_len;
        if (blk_flag & 0x80U) { blk_num = frame[desc_pos+1]; desc_len = 2; }
        else                   { blk_num = frame[desc_pos+1]; desc_len = 3; }
        desc_pos += desc_len;
        if (blk_num >= s_card.block_count) {
            memset(&resp[rp], 0x00, FELICA_BLOCK_SIZE);
        } else {
            memcpy(&resp[rp], &s_card.mem[blk_num * FELICA_BLOCK_SIZE], FELICA_BLOCK_SIZE);
        }
        rp += FELICA_BLOCK_SIZE;
    }
    resp[0] = (uint8_t)rp;
    ESP_LOGI(TAG, "READ blocks=%d", blk_count);
    tx_response(resp, rp);
}

static void handle_write(const uint8_t* frame, int len)
{
    if (len < 32) { ESP_LOGW(TAG, "Write: too short (%d)", len); return; }
    if (memcmp(&frame[2], s_card.idm, FELICA_IDM_LEN) != 0) { return; }
    uint8_t sc_count = frame[10];
    int     blk_pos  = 11 + sc_count * 2;
    if (blk_pos >= len) { return; }
    uint8_t blk_count = frame[blk_pos];
    int     pos       = blk_pos + 1;
    if (blk_count == 0) return;
    uint8_t status1 = 0x00U;
    for (int i = 0; i < blk_count; i++) {
        if (pos + 2 + FELICA_BLOCK_SIZE > len) { status1 = 0x01U; break; }
        uint8_t blk_flag = frame[pos];
        uint8_t blk_num;
        int     desc_len;
        if (blk_flag & 0x80U) { blk_num = frame[pos+1]; desc_len = 2; }
        else                   { blk_num = frame[pos+1]; desc_len = 3; }
        if (pos + desc_len + FELICA_BLOCK_SIZE > len) { status1 = 0x01U; break; }
        if (blk_num < s_card.block_count) {
            memcpy(&s_card.mem[blk_num * FELICA_BLOCK_SIZE],
                   &frame[pos + desc_len], FELICA_BLOCK_SIZE);
        } else { status1 = 0x01U; }
        pos += desc_len + FELICA_BLOCK_SIZE;
    }
    uint8_t resp[12];
    resp[0] = 12U; resp[1] = FELICA_CMD_WRITE_RESP;
    memcpy(&resp[2], s_card.idm, FELICA_IDM_LEN);
    resp[10] = status1; resp[11] = 0x00U;
    tx_response(resp, 12);
}

void felica_emu_card_from_tag(felica_emu_card_t* card, const felica_tag_t* tag,
                               uint16_t sc, uint8_t bc, const uint8_t* mem)
{
    if (!card || !tag) return;
    memset(card, 0, sizeof(*card));
    memcpy(card->idm, tag->idm, FELICA_IDM_LEN);
    memcpy(card->pmm, tag->pmm, FELICA_PMM_LEN);
    card->system_code  = tag->rd_valid ? (uint16_t)((tag->rd[0]<<8)|tag->rd[1]) : FELICA_SC_COMMON;
    card->service_code = sc;
    card->block_count  = bc;
    if (mem) {
        size_t bytes = (size_t)bc * FELICA_BLOCK_SIZE;
        if (bytes > FELICA_EMU_MEM_SIZE) bytes = FELICA_EMU_MEM_SIZE;
        memcpy(card->mem, mem, bytes);
    }
}

void felica_emu_card_default(felica_emu_card_t* card,
                              const uint8_t idm[FELICA_IDM_LEN],
                              const uint8_t pmm[FELICA_PMM_LEN])
{
    if (!card) return;
    memset(card, 0, sizeof(*card));
    memcpy(card->idm, idm, FELICA_IDM_LEN);
    memcpy(card->pmm, pmm, FELICA_PMM_LEN);
    card->system_code  = FELICA_SC_COMMON;
    card->service_code = 0x0009U;
    card->block_count  = 16;
}

hb_nfc_err_t felica_emu_init(const felica_emu_card_t* card)
{
    if (!card || card->block_count > FELICA_EMU_MAX_BLOCKS) return HB_NFC_ERR_PARAM;
    memcpy(&s_card, card, sizeof(s_card));
    s_state = FELICA_STATE_SLEEP; s_idle = 0; s_init_done = true;
    char idm_s[24];
    snprintf(idm_s, sizeof(idm_s), "%02X%02X%02X%02X%02X%02X%02X%02X",
             card->idm[0],card->idm[1],card->idm[2],card->idm[3],
             card->idm[4],card->idm[5],card->idm[6],card->idm[7]);
    ESP_LOGI(TAG, "Init: IDm=%s SC=0x%04X blocks=%d", idm_s, card->system_code, card->block_count);
    return HB_NFC_OK;
}

hb_nfc_err_t felica_emu_configure_target(void)
{
    if (!s_init_done) return HB_NFC_ERR_INTERNAL;
    ESP_LOGI(TAG, "Configure NFC-F target");
    hb_spi_reg_write(REG_OP_CTRL, 0x00U);
    vTaskDelay(pdMS_TO_TICKS(5));
    hb_spi_direct_cmd(CMD_SET_DEFAULT);
    vTaskDelay(pdMS_TO_TICKS(10));
    hb_spi_reg_write(REG_ISO14443A, 0x10U);
    vTaskDelay(pdMS_TO_TICKS(2));
    uint8_t ic = 0;
    hb_spi_reg_read(REG_IC_IDENTITY, &ic);
    if (ic == 0x00U || ic == 0xFFU) { ESP_LOGE(TAG, "Chip not responding: IC=0x%02X", ic); return HB_NFC_ERR_INTERNAL; }
    ESP_LOGI(TAG, "Chip OK: IC=0x%02X", ic);
    hb_spi_reg_write(REG_IO_CONF2, 0x80U);
    vTaskDelay(pdMS_TO_TICKS(2));
    hb_spi_reg_write(REG_OP_CTRL, 0x80U);
    wait_oscillator(200);
    hb_spi_direct_cmd(CMD_ADJUST_REGULATORS);
    vTaskDelay(pdMS_TO_TICKS(10));
    hb_spi_reg_write(REG_MODE,    MODE_TARGET_NFCF);
    hb_spi_reg_write(REG_AUX_DEF, FELICA_AUX_DEF);
    hb_spi_reg_write(REG_BIT_RATE,         FELICA_BITRATE_212);
    hb_spi_reg_write(REG_PASSIVE_TARGET,   0x00U);
    hb_spi_reg_write(REG_ISO14443B_FELICA, FELICA_ISO_F_CTRL);
    hb_spi_reg_write(REG_FIELD_THRESH_ACT,   0x00U);
    hb_spi_reg_write(REG_FIELD_THRESH_DEACT, 0x00U);
    hb_spi_reg_write(REG_PT_MOD, 0x60U);
    write_pt_mem_f();
    st25r_irq_read();
    hb_spi_reg_write(REG_MASK_MAIN_INT,      0x00U);
    hb_spi_reg_write(REG_MASK_TIMER_NFC_INT, 0x00U);
    hb_spi_reg_write(REG_MASK_ERROR_WUP_INT, 0x00U);
    hb_spi_reg_write(REG_MASK_TARGET_INT,    0x00U);
    uint8_t mode_rb = 0, aux_rb = 0, isof_rb = 0;
    hb_spi_reg_read(REG_MODE,             &mode_rb);
    hb_spi_reg_read(REG_AUX_DEF,          &aux_rb);
    hb_spi_reg_read(REG_ISO14443B_FELICA, &isof_rb);
    if (mode_rb != MODE_TARGET_NFCF) { ESP_LOGE(TAG, "MODE mismatch: 0x%02X", mode_rb); return HB_NFC_ERR_INTERNAL; }
    ESP_LOGI(TAG, "Configured: MODE=0x%02X AUX_DEF=0x%02X ISO_F=0x%02X", mode_rb, aux_rb, isof_rb);
    return HB_NFC_OK;
}

hb_nfc_err_t felica_emu_start(void)
{
    if (!s_init_done) return HB_NFC_ERR_INTERNAL;
    st25r_irq_read();
    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_TARGET);
    vTaskDelay(pdMS_TO_TICKS(5));
    hb_spi_direct_cmd(CMD_GOTO_SLEEP);
    vTaskDelay(pdMS_TO_TICKS(2));
    s_state = FELICA_STATE_SLEEP; s_idle = 0;
    uint8_t op=0, aux=0, pts=0;
    hb_spi_reg_read(REG_OP_CTRL,            &op);
    hb_spi_reg_read(REG_AUX_DISPLAY,        &aux);
    hb_spi_reg_read(REG_PASSIVE_TARGET_STS, &pts);
    ESP_LOGI(TAG, "Start (SLEEP): OP=0x%02X AUX=0x%02X PT_STS=0x%02X", op, aux, pts);
    ESP_LOGI(TAG, "FeliCa emulation active - present a reader!");
    return HB_NFC_OK;
}

void felica_emu_stop(void)
{
    hb_spi_reg_write(REG_OP_CTRL, 0x00U);
    s_state = FELICA_STATE_SLEEP; s_idle = 0; s_init_done = false;
    ESP_LOGI(TAG, "FeliCa emulation stopped");
}

/* ── run_step ── DIAGNOSTIC BUILD ────────────────────────────────────────────
 *
 * Logs ALL IRQ register values on EVERY SENSE entry and on field-lost
 * to pinpoint exactly which register/bit causes the premature SLEEP.
 *
 * Key question: is it timer_irq (REG_TIMER_NFC_INT 0x1B) bit3,
 * or tgt_irq (REG_TARGET_INT 0x1D) bit3 (IRQ_TGT_OSCF)?
 * Or is it something else entirely?
 * ─────────────────────────────────────────────────────────────────────────── */
void felica_emu_run_step(void)
{
    static uint32_t s_alive = 0;
    if ((++s_alive % 10000U) == 0U) {
        uint8_t aux=0, pts=0, op=0;
        hb_spi_reg_read(REG_AUX_DISPLAY,        &aux);
        hb_spi_reg_read(REG_PASSIVE_TARGET_STS, &pts);
        hb_spi_reg_read(REG_OP_CTRL,            &op);
        ESP_LOGI(TAG, "alive | state=%s OP=%02X AUX=%02X efd=%d pta=%d",
                 s_state==FELICA_STATE_SLEEP?"SLEEP":s_state==FELICA_STATE_SENSE?"SENSE":"ACTIVE",
                 op, aux, (aux>>6)&1, pts&0x0FU);
    }

    uint8_t tgt_irq   = 0;
    uint8_t main_irq  = 0;
    uint8_t timer_irq = 0;
    uint8_t err_irq   = 0;
    hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
    hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
    hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);
    hb_spi_reg_read(REG_ERROR_INT,     &err_irq);   /* read all 4 regs */

    uint8_t pts = 0;
    hb_spi_reg_read(REG_PASSIVE_TARGET_STS, &pts);
    uint8_t pta = pts & 0x0FU;

    /* ── SLEEP ── */
    if (s_state == FELICA_STATE_SLEEP) {
        bool wake = (timer_irq & TIMER_I_EON)
                 || (tgt_irq & IRQ_TGT_WU_F)
                 || (tgt_irq & IRQ_TGT_WU_A);
        if (!wake) return;

        ESP_LOGI(TAG, "SLEEP -> SENSE (pta=%u tgt=0x%02X main=0x%02X tmr=0x%02X err=0x%02X)",
                 pta, tgt_irq, main_irq, timer_irq, err_irq);

        hb_spi_direct_cmd(CMD_GOTO_SENSE);
        s_state = FELICA_STATE_SENSE;
        s_idle  = 0;
        return; /* [BUG-14 fix] no fall-through */
    }

    /* ── SENSE ── */
    if (s_state == FELICA_STATE_SENSE) {

        /* ── DIAGNOSTIC: log every non-idle IRQ in SENSE ── */
        if (tgt_irq || main_irq || timer_irq || err_irq) {
            uint8_t rssi = 0;
            hb_spi_reg_read(REG_RSSI_RESULT, &rssi);
            ESP_LOGI(TAG, "[SENSE-IRQ] tgt=0x%02X main=0x%02X tmr=0x%02X err=0x%02X pta=%u RSSI=0x%02X",
                     tgt_irq, main_irq, timer_irq, err_irq, pta, rssi);
            /*
             * Decode bits for easy reading:
             *   tgt_irq bits: 7=WU_A 6=WU_A_X 5=WU_F 4=rfu 3=OSCF 2=SDD_C 1=rfu 0=rfu
             *   main_irq bits: 7=OSC 6=rfu 5=RXS 4=RXE(was FWL) 3=TXE 2=RXE 1=COL 0=rfu
             *   timer_irq bits: 7=GPT 6=NRT 5=MRAT 4=rfu 3=??? 2=??? 1=??? 0=???
             *
             * NOTE: the 0x08 in timer_irq — which bit IS it exactly?
             * The log will reveal whether it's timer_irq=0x08 or tgt_irq=0x08
             * that triggers field-lost.
             */
        }

        bool go_active = (tgt_irq & IRQ_TGT_WU_F)
                      || (main_irq & IRQ_MAIN_RXE);

        if (go_active) {
            s_idle = 0;
            ESP_LOGI(TAG, "SENSE -> ACTIVE (pta=%u tgt=0x%02X main=0x%02X tmr=0x%02X)",
                     pta, tgt_irq, main_irq, timer_irq);
            s_state = FELICA_STATE_ACTIVE;
            if (!(main_irq & IRQ_MAIN_RXE)) return;

        } else if (timer_irq & TIMER_I_EOF) {
            uint8_t rssi = 0;
            hb_spi_reg_read(REG_RSSI_RESULT, &rssi);
            ESP_LOGW(TAG, "SENSE: field lost -> SLEEP | timer_irq=0x%02X tgt=0x%02X pta=%u RSSI=0x%02X",
                     timer_irq, tgt_irq, pta, rssi);
            goto_sleep();
            return;

        } else if (!tgt_irq && !main_irq && !timer_irq) {
            if ((++s_idle) >= SENSE_IDLE_TIMEOUT) {
                ESP_LOGI(TAG, "SENSE: timeout -> SLEEP");
                goto_sleep();
                return;
            }
            if ((s_idle % 5000U) == 0U) {
                uint8_t aux=0, rssi=0;
                hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
                hb_spi_reg_read(REG_RSSI_RESULT,  &rssi);
                ESP_LOGI(TAG, "[SENSE] idle=%lu AUX=0x%02X pta=%d RSSI=0x%02X",
                         (unsigned long)s_idle, aux, pta, rssi);
            }
            return;
        } else {
            return;
        }
    }

    /* ── ACTIVE ── */
    if (timer_irq & TIMER_I_EOF) {
        uint8_t aux=0;
        hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
        ESP_LOGI(TAG, "ACTIVE: field lost (AUX=0x%02X) -> SLEEP", aux);
        goto_sleep();
        return;
    }
    if (!(main_irq & IRQ_MAIN_RXE)) {
        if ((++s_idle) >= ACTIVE_IDLE_TIMEOUT) {
            ESP_LOGI(TAG, "ACTIVE: idle timeout -> SLEEP");
            goto_sleep();
        }
        return;
    }
    s_idle = 0;

    uint8_t buf[256] = {0};
    uint8_t fs1 = 0;
    hb_spi_reg_read(REG_FIFO_STATUS1, &fs1);
    int len = (int)(fs1 & 0x7FU);
    if (len <= 0) return;
    if (len > (int)sizeof(buf)) len = (int)sizeof(buf);
    hb_spi_fifo_read(buf, (uint8_t)len);
    if (len < 2) return;

    uint8_t cmd_code = buf[1];
    ESP_LOGI(TAG, "CMD 0x%02X len=%d", cmd_code, len);
    switch (cmd_code) {
    case FELICA_CMD_READ_WO_ENC:  handle_read(buf, len);  break;
    case FELICA_CMD_WRITE_WO_ENC: handle_write(buf, len); break;
    default: ESP_LOGW(TAG, "Unsupported cmd 0x%02X", cmd_code); break;
    }
}