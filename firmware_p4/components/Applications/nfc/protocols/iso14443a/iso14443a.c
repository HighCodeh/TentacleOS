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
 * @file iso14443a.c
 * @brief ISO14443A CRC_A calculation.
 */
#include "iso14443a.h"

void iso14443a_crc(const uint8_t* data, size_t len, uint8_t crc[2])
{
    uint32_t wCrc = 0x6363;
    for (size_t i = 0; i < len; i++) {
        uint8_t bt = data[i];
        bt = (bt ^ (uint8_t)(wCrc & 0x00FF));
        bt = (bt ^ (bt << 4));
        wCrc = (wCrc >> 8) ^
               ((uint32_t)bt << 8) ^
               ((uint32_t)bt << 3) ^
               ((uint32_t)bt >> 4);
    }
    crc[0] = (uint8_t)(wCrc & 0xFF);
    crc[1] = (uint8_t)((wCrc >> 8) & 0xFF);
}

bool iso14443a_check_crc(const uint8_t* data, size_t len)
{
    if (len < 3) return false;
    uint8_t crc[2];
    iso14443a_crc(data, len - 2, crc);
    return (crc[0] == data[len - 2]) && (crc[1] == data[len - 1]);
}

/**
 * @file poller.c
 * @brief ISO14443A poller helpers.
 */
#include "poller.h"
#include "iso14443a.h"
#include "nfc_poller.h"
#include "nfc_common.h"
#include "st25r3916_cmd.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"
#include "esp_timer.h"
#include "nfc_rf.h"

#include "esp_log.h"

#define TAG TAG_14443A
static const char* TAG = "14443a";

/**
 * Enable or disable anti-collision.
 */
static void set_antcl(bool enable)
{
    uint8_t v;
    hb_spi_reg_read(REG_ISO14443A, &v);
    if (enable) v |= ISO14443A_ANTCL;
    else        v &= (uint8_t)~ISO14443A_ANTCL;
    hb_spi_reg_write(REG_ISO14443A, v);
}

/**
 * Send REQA or WUPA and read ATQA.
 */
static int req_cmd(uint8_t cmd, uint8_t atqa[2])
{
    set_antcl(false);
    st25r_fifo_clear();
    hb_spi_direct_cmd(cmd);

    
    if (!st25r_irq_wait_txe()) return 0;

    
    uint16_t count = 0;
    if (st25r_fifo_wait(2, 10, &count) < 2) {
        st25r_irq_log((cmd == CMD_TX_REQA) ? "REQA fail" : "WUPA fail", count);
        return 0;
    }
    st25r_fifo_read(atqa, 2);
    return 2;
}

int iso14443a_poller_reqa(uint8_t atqa[2])
{
    return req_cmd(CMD_TX_REQA, atqa);
}

int iso14443a_poller_wupa(uint8_t atqa[2])
{
    return req_cmd(CMD_TX_WUPA, atqa);
}

/**
 * Activate a card with REQA/WUPA.
 */
hb_nfc_err_t iso14443a_poller_activate(uint8_t atqa[2])
{
    if (req_cmd(CMD_TX_REQA, atqa) == 2) return HB_NFC_OK;
    hb_delay_ms(5);
    if (req_cmd(CMD_TX_WUPA, atqa) == 2) return HB_NFC_OK;
    return HB_NFC_ERR_NO_CARD;
}

/**
 * Run anti-collision for one cascade level.
 */
int iso14443a_poller_anticoll(uint8_t sel, uint8_t uid_cl[5])
{
    uint8_t cmd[2] = { sel, 0x20 };

    for (int attempt = 0; attempt < 3; attempt++) {
        set_antcl(true);
        int len = nfc_poller_transceive(cmd, 2, false, uid_cl, 5, 5, 20);
        set_antcl(false);

        if (len == 5) return 5;
        hb_delay_ms(5);
    }
    return 0;
}

/**
 * Run SELECT for one cascade level.
 */
int iso14443a_poller_sel(uint8_t sel, const uint8_t uid_cl[5], uint8_t* sak)
{
    uint8_t cmd[7] = {
        sel, 0x70,
        uid_cl[0], uid_cl[1], uid_cl[2], uid_cl[3], uid_cl[4]
    };
    uint8_t rx[4] = { 0 };
    int len = nfc_poller_transceive(cmd, 7, true, rx, sizeof(rx), 1, 10);
    if (len < 1) return 0;
    *sak = rx[0];
    return 1;
}

static hb_nfc_err_t iso14443a_select_from_atqa(const uint8_t atqa[2], nfc_iso14443a_data_t* card)
{
    if (!card || !atqa) return HB_NFC_ERR_PARAM;

    card->atqa[0] = atqa[0];
    card->atqa[1] = atqa[1];
    card->uid_len = 0;

    static const uint8_t sel_cmds[] = { SEL_CL1, SEL_CL2, SEL_CL3 };

    for (int cl = 0; cl < 3; cl++) {
        uint8_t uid_cl[5] = { 0 };
        uint8_t sak = 0;

        if (iso14443a_poller_anticoll(sel_cmds[cl], uid_cl) != 5) {
            ESP_LOGW(TAG, "Anticoll CL%d failed", cl + 1);
            return HB_NFC_ERR_COLLISION;
        }
        if (!iso14443a_poller_sel(sel_cmds[cl], uid_cl, &sak)) {
            ESP_LOGW(TAG, "Select CL%d failed", cl + 1);
            return HB_NFC_ERR_PROTOCOL;
        }

        if (uid_cl[0] == 0x88) {
            card->uid[card->uid_len++] = uid_cl[1];
            card->uid[card->uid_len++] = uid_cl[2];
            card->uid[card->uid_len++] = uid_cl[3];
        } else {
            card->uid[card->uid_len++] = uid_cl[0];
            card->uid[card->uid_len++] = uid_cl[1];
            card->uid[card->uid_len++] = uid_cl[2];
            card->uid[card->uid_len++] = uid_cl[3];
        }

        card->sak = sak;
        if (!(sak & 0x04)) break;
    }

    return HB_NFC_OK;
}

/**
 * Perform full card selection across cascade levels.
 */
hb_nfc_err_t iso14443a_poller_select(nfc_iso14443a_data_t* card)
{
    if (!card) return HB_NFC_ERR_PARAM;

    uint8_t atqa[2];
    hb_nfc_err_t err = iso14443a_poller_activate(atqa);
    if (err != HB_NFC_OK) return err;

    nfc_log_hex("ATQA:", atqa, 2);
    hb_nfc_err_t sel_err = iso14443a_select_from_atqa(atqa, card);
    if (sel_err != HB_NFC_OK) return sel_err;
    nfc_log_hex("UID:", card->uid, card->uid_len);
    return HB_NFC_OK;
}

/**
 * Re-select a card after Crypto1 session.
 *
 * After Crypto1 authentication the card is in AUTHENTICATED state
 * and will NOT respond to WUPA/REQA. We must power-cycle the RF field
 * to force the card back to IDLE state, then do full activation.
 *
 * Flow: field OFF field ON REQA/WUPA anticoll select (all CLs)
 */
hb_nfc_err_t iso14443a_poller_reselect(nfc_iso14443a_data_t* card)
{
    
    st25r_field_cycle();

    
    uint8_t atqa[2];
    hb_nfc_err_t err = iso14443a_poller_activate(atqa);
    if (err != HB_NFC_OK) return err;

    
    static const uint8_t sel_cmds[] = { SEL_CL1, SEL_CL2, SEL_CL3 };

    for (int cl = 0; cl < 3; cl++) {
        uint8_t uid_cl[5] = { 0 };
        uint8_t sak = 0;

        if (iso14443a_poller_anticoll(sel_cmds[cl], uid_cl) != 5) {
            return HB_NFC_ERR_COLLISION;
        }
        if (!iso14443a_poller_sel(sel_cmds[cl], uid_cl, &sak)) {
            return HB_NFC_ERR_PROTOCOL;
        }

        card->sak = sak;

        
        if (!(sak & 0x04)) break;
    }

    return HB_NFC_OK;
}

hb_nfc_err_t iso14443a_poller_halt(void)
{
    uint8_t cmd[2] = { 0x50, 0x00 };
    uint8_t rx[4] = { 0 };
    (void)nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 0, 10);
    return HB_NFC_OK;
}

int iso14443a_poller_select_all(nfc_iso14443a_data_t* out, size_t max_cards)
{
    if (!out || max_cards == 0) return 0;

    size_t count = 0;
    for (;;) {
        if (count >= max_cards) break;

        uint8_t atqa[2] = { 0 };
        if (iso14443a_poller_reqa(atqa) != 2) break;

        nfc_iso14443a_data_t card = { 0 };
        if (iso14443a_select_from_atqa(atqa, &card) != HB_NFC_OK) break;

        out[count++] = card;
        (void)iso14443a_poller_halt();
        hb_delay_ms(5);
    }

    return (int)count;
}
#undef TAG

/**
 * @file nfc_poller.c
 * @brief NFC poller transceive engine.
 */
#include "nfc_poller.h"
#include "nfc_common.h"
#include "st25r3916_core.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"
#include <stdio.h>
#include "esp_log.h"

#define TAG TAG_NFC_POLL
static const char* TAG = "nfc_poll";

static uint32_t s_guard_time_us = 0;
static uint32_t s_fdt_min_us = 0;
static bool     s_validate_fdt = false;
static uint32_t s_last_fdt_us = 0;

hb_nfc_err_t nfc_poller_start(void)
{
    nfc_rf_config_t cfg = {
        .tech            = NFC_RF_TECH_A,
        .tx_rate         = NFC_RF_BR_106,
        .rx_rate         = NFC_RF_BR_106,
        .am_mod_percent  = 0, /* OOK for ISO-A */
        .tx_parity       = true,
        .rx_raw_parity   = false,
        .guard_time_us   = 0,
        .fdt_min_us      = 0,
        .validate_fdt    = false,
    };
    hb_nfc_err_t err = nfc_rf_apply(&cfg);
    if (err != HB_NFC_OK) return err;
    return st25r_field_on();
}

void nfc_poller_stop(void)
{
    st25r_field_off();
}

/**
 * Transmit a frame and wait for the response in FIFO.
 */
int nfc_poller_transceive(const uint8_t* tx, size_t tx_len, bool with_crc,
                           uint8_t* rx, size_t rx_max, size_t rx_min,
                           int timeout_ms)
{
    if (s_guard_time_us > 0) {
        hb_delay_us(s_guard_time_us);
    }

    int64_t t0 = esp_timer_get_time();

    
    st25r_fifo_clear();

    
    st25r_set_tx_bytes((uint16_t)tx_len, 0);

    
    st25r_fifo_load(tx, tx_len);

    
    hb_spi_direct_cmd(with_crc ? CMD_TX_WITH_CRC : CMD_TX_WO_CRC);

    
    if (!st25r_irq_wait_txe()) {
        ESP_LOGW(TAG, "TX timeout");
        return 0;
    }

    
    uint16_t count = 0;
    int got = st25r_fifo_wait(rx_min, timeout_ms, &count);

    int64_t t1 = esp_timer_get_time();
    if (t1 > t0) {
        s_last_fdt_us = (uint32_t)(t1 - t0);
        if (s_validate_fdt && s_fdt_min_us > 0 && s_last_fdt_us < s_fdt_min_us) {
            ESP_LOGW(TAG, "FDT below min: %uus < %uus", (unsigned)s_last_fdt_us, (unsigned)s_fdt_min_us);
        }
    }

    
    if (count < rx_min) {
        if (count > 0) {
            size_t to_read = (count > rx_max) ? rx_max : count;
            st25r_fifo_read(rx, to_read);
            nfc_log_hex(" RX partial:", rx, to_read);
        }
        st25r_irq_log("RX fail", count);
        return 0;
    }

    if ((size_t)got > rx_max) got = (int)rx_max;
    st25r_fifo_read(rx, (size_t)got);
    return got;
}

void nfc_poller_set_timing(uint32_t guard_time_us, uint32_t fdt_min_us, bool validate_fdt)
{
    s_guard_time_us = guard_time_us;
    s_fdt_min_us = fdt_min_us;
    s_validate_fdt = validate_fdt;
}

uint32_t nfc_poller_get_last_fdt_us(void)
{
    return s_last_fdt_us;
}

void nfc_log_hex(const char* label, const uint8_t* data, size_t len)
{
    char buf[128];
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3 < sizeof(buf); i++) {
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%02X%s",
                                data[i], (i + 1 < len) ? " " : "");
    }
    ESP_LOGI("nfc", "%s %s", label, buf);
}
#undef TAG

/**
 * @file iso_dep.c
 * @brief ISO-DEP - basic RATS + I-Block exchange with chaining/WTX (best effort).
 */
#include "iso_dep.h"
#include "nfc_poller.h"
#include "esp_log.h"

#define TAG TAG_ISO_DEP
static const char* TAG = "iso_dep";

/* ISO-DEP PCB helpers (best-effort values) */
#define ISO_DEP_PCB_I_BLOCK   0x02U
#define ISO_DEP_PCB_I_CHAIN   0x10U
#define ISO_DEP_PCB_R_ACK     0xA2U
#define ISO_DEP_PCB_S_WTX     0xF2U

static size_t iso_dep_fsc_from_fsci(uint8_t fsci)
{
    static const uint16_t tbl[] = { 16, 24, 32, 40, 48, 64, 96, 128, 256 };
    if (fsci > 8) fsci = 8;
    return tbl[fsci];
}

static int iso_dep_fwt_ms(uint8_t fwi)
{
    /* FWT = 302us * 2^FWI; clamp to sane minimum */
    uint32_t us = 302U * (1U << (fwi & 0x0FU));
    int ms = (int)((us + 999U) / 1000U);
    if (ms < 5) ms = 5;
    return ms;
}

static void iso_dep_parse_ats(nfc_iso_dep_data_t* dep)
{
    if (!dep || dep->ats_len < 2) return;

    uint8_t t0 = dep->ats[1];
    uint8_t fsci = (uint8_t)(t0 & 0x0F);
    size_t fsc = iso_dep_fsc_from_fsci(fsci);
    if (fsc > 255) fsc = 255;
    dep->fsc = (uint8_t)fsc;

    int pos = 2;
    /* TA present */
    if (t0 & 0x10U) pos++;
    /* TB present: FWI in upper nibble */
    if (t0 & 0x20U) {
        if ((size_t)pos < dep->ats_len) {
            uint8_t tb = dep->ats[pos];
            dep->fwi = (uint8_t)((tb >> 4) & 0x0F);
        }
        pos++;
    }
    /* TC present */
    if (t0 & 0x40U) pos++;

    ESP_LOGI(TAG, "ATS parsed: FSCI=%u FSC=%u FWI=%u",
             (unsigned)fsci, (unsigned)dep->fsc, (unsigned)dep->fwi);
}

static bool iso_dep_is_i_block(uint8_t pcb) { return (pcb & 0xC0U) == 0x00U; }
static bool iso_dep_is_r_block(uint8_t pcb) { return (pcb & 0xC0U) == 0x80U; }
static bool iso_dep_is_s_block(uint8_t pcb) { return (pcb & 0xC0U) == 0xC0U; }
static bool iso_dep_is_wtx(uint8_t pcb)     { return iso_dep_is_s_block(pcb) && ((pcb & 0x0FU) == 0x02U); }

static int iso_dep_strip_crc(uint8_t* buf, int len)
{
    if (len >= 3 && iso14443a_check_crc(buf, (size_t)len)) return len - 2;
    return len;
}

hb_nfc_err_t iso_dep_rats(uint8_t fsdi, uint8_t cid, nfc_iso_dep_data_t* dep)
{
    uint8_t cmd[2] = { 0xE0U, (uint8_t)((fsdi << 4) | (cid & 0x0F)) };
    uint8_t rx[64] = { 0 };
    int len = nfc_poller_transceive(cmd, 2, true, rx, sizeof(rx), 1, 30);
    if (len < 1) {
        ESP_LOGW(TAG, "RATS failed");
        return HB_NFC_ERR_PROTOCOL;
    }
    if (len >= 3 && iso14443a_check_crc(rx, (size_t)len)) {
        len -= 2;
    }
    if (dep) {
        dep->ats_len = (size_t)len;
        for (int i = 0; i < len && i < NFC_ATS_MAX_LEN; i++) {
            dep->ats[i] = rx[i];
        }
        dep->fsc = 32;
        dep->fwi = 4;
        iso_dep_parse_ats(dep);
    }
    return HB_NFC_OK;
}

hb_nfc_err_t iso_dep_pps(uint8_t cid, uint8_t dsi, uint8_t dri)
{
    /* Best-effort PPS. If dsi/dri are 0, keep default 106 kbps. */
    if ((dsi == 0) && (dri == 0)) return HB_NFC_OK;
    uint8_t cmd[3] = {
        (uint8_t)(0xD0U | (cid & 0x0FU)),
        0x11U, /* PPS0: PPS1 present */
        (uint8_t)(((dsi & 0x03U) << 2) | (dri & 0x03U))
    };
    uint8_t rx[4] = { 0 };
    int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, 20);
    if (len < 1) {
        ESP_LOGW(TAG, "PPS failed");
        return HB_NFC_ERR_PROTOCOL;
    }
    return HB_NFC_OK;
}

int iso_dep_transceive(const nfc_iso_dep_data_t* dep,
                        const uint8_t*           tx,
                        size_t                   tx_len,
                        uint8_t*                 rx,
                        size_t                   rx_max,
                        int                      timeout_ms)
{
    if (!tx || !rx || tx_len == 0) return 0;

    size_t fsc = (dep && dep->fsc) ? dep->fsc : 32;
    size_t max_inf = (fsc > 1) ? (fsc - 1U) : tx_len;
    int base_timeout = (timeout_ms > 0) ? timeout_ms
                                        : iso_dep_fwt_ms(dep ? dep->fwi : 4);

    uint8_t rbuf[260] = { 0 };
    int rlen = 0;
    uint8_t block_num = 0;

    /* TX chaining (PCD -> PICC) */
    size_t off = 0;
    while (off < tx_len) {
        size_t chunk = tx_len - off;
        bool chaining = chunk > max_inf;
        if (chaining) chunk = max_inf;

        uint8_t frame[1 + 256];
        frame[0] = (uint8_t)(ISO_DEP_PCB_I_BLOCK | (block_num & 0x01U));
        if (chaining) frame[0] |= ISO_DEP_PCB_I_CHAIN;
        memcpy(&frame[1], &tx[off], chunk);

        rlen = nfc_poller_transceive(frame, 1 + chunk, true,
                                     rbuf, sizeof(rbuf), 1, base_timeout);
        if (rlen <= 0) return 0;
        rlen = iso_dep_strip_crc(rbuf, rlen);

        if (chaining) {
            if (!iso_dep_is_r_block(rbuf[0])) {
                ESP_LOGW(TAG, "Expected R-block, got 0x%02X", rbuf[0]);
                return 0;
            }
            off += chunk;
            block_num ^= 1U;
            continue;
        }

        off += chunk;
        break;
    }

    /* RX chaining + WTX handling (PICC -> PCD) */
    int out_len = 0;
    while (1) {
        uint8_t pcb = rbuf[0];

        if (iso_dep_is_wtx(pcb)) {
            uint8_t wtxm = (rlen >= 2) ? rbuf[1] : 0x01U;
            uint8_t wtx_resp[2] = { ISO_DEP_PCB_S_WTX, wtxm };
            int tmo = base_timeout * (wtxm ? wtxm : 1U);
            rlen = nfc_poller_transceive(wtx_resp, sizeof(wtx_resp), true,
                                         rbuf, sizeof(rbuf), 1, tmo);
            if (rlen <= 0) return 0;
            rlen = iso_dep_strip_crc(rbuf, rlen);
            continue;
        }

        if (iso_dep_is_i_block(pcb)) {
            bool chaining = (pcb & ISO_DEP_PCB_I_CHAIN) != 0U;
            size_t inf_len = (rlen > 1) ? (size_t)(rlen - 1) : 0;
            if ((size_t)out_len + inf_len > rx_max) {
                inf_len = (rx_max > (size_t)out_len) ? (rx_max - (size_t)out_len) : 0;
            }
            if (inf_len > 0) memcpy(&rx[out_len], &rbuf[1], inf_len);
            out_len += (int)inf_len;

            if (!chaining) return out_len;

            uint8_t rack[1] = { (uint8_t)(ISO_DEP_PCB_R_ACK | (pcb & 0x01U)) };
            rlen = nfc_poller_transceive(rack, sizeof(rack), true,
                                         rbuf, sizeof(rbuf), 1, base_timeout);
            if (rlen <= 0) return out_len ? out_len : 0;
            rlen = iso_dep_strip_crc(rbuf, rlen);
            continue;
        }

        if (iso_dep_is_r_block(pcb)) {
            ESP_LOGW(TAG, "Unexpected R-block 0x%02X", pcb);
            return out_len;
        }

        ESP_LOGW(TAG, "Unknown PCB 0x%02X", pcb);
        return out_len;
    }
}

int iso_dep_apdu_transceive(const nfc_iso_dep_data_t* dep,
                             const uint8_t*           apdu,
                             size_t                   apdu_len,
                             uint8_t*                 rx,
                             size_t                   rx_max,
                             int                      timeout_ms)
{
    if (!apdu || !rx || apdu_len == 0) return 0;

    uint8_t cmd_buf[300];
    if (apdu_len > sizeof(cmd_buf)) return 0;
    memcpy(cmd_buf, apdu, apdu_len);

    int len = iso_dep_transceive(dep, cmd_buf, apdu_len, rx, rx_max, timeout_ms);
    if (len < 2) return len;

    /* SW1/SW2 are last bytes */
    uint8_t sw1 = rx[len - 2];
    uint8_t sw2 = rx[len - 1];

    /* Handle wrong length (0x6C) by retrying with suggested Le */
    if (sw1 == 0x6CU && apdu_len >= 5) {
        cmd_buf[apdu_len - 1] = sw2; /* adjust Le */
        len = iso_dep_transceive(dep, cmd_buf, apdu_len, rx, rx_max, timeout_ms);
        return len;
    }

    /* Handle GET RESPONSE (0x61) */
    if (sw1 == 0x61U) {
        size_t out_pos = (size_t)(len - 2); /* keep data from first response */
        uint8_t le = sw2 ? sw2 : 0x00;      /* 0x00 means 256 */

        for (int i = 0; i < 8; i++) {
            uint8_t get_resp[5] = { 0x00, 0xC0, 0x00, 0x00, le };
            uint8_t tmp[260] = { 0 };
            int rlen = iso_dep_transceive(dep, get_resp, sizeof(get_resp),
                                          tmp, sizeof(tmp), timeout_ms);
            if (rlen < 2) return (int)out_pos;

            uint8_t rsw1 = tmp[rlen - 2];
            uint8_t rsw2 = tmp[rlen - 1];
            int data_len = rlen - 2;
            if (data_len > 0) {
                size_t copy = (out_pos + (size_t)data_len <= rx_max)
                                ? (size_t)data_len
                                : (rx_max > out_pos ? (rx_max - out_pos) : 0);
                if (copy > 0) {
                    memcpy(&rx[out_pos], tmp, copy);
                    out_pos += copy;
                }
            }

            sw1 = rsw1;
            sw2 = rsw2;
            if (sw1 != 0x61U) {
                break;
            }
            le = sw2 ? sw2 : 0x00;
        }

        if (out_pos + 2 <= rx_max) {
            rx[out_pos++] = sw1;
            rx[out_pos++] = sw2;
        }
        return (int)out_pos;
    }

    return len;
}
#undef TAG

