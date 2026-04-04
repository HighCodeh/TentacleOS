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
 * @file iso14443b.c
 * @brief ISO 14443B (NFC-B) poller basics for ST25R3916.
 *
 * Best-effort implementation: REQB/ATQB + ATTRIB with default params.
 */
#include "iso14443b.h"

#include <string.h>
#include "esp_log.h"

#include "nfc_poller.h"
#include "nfc_common.h"
#include "t4t.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "nfc_rf.h"

#define TAG "iso14443b"

#define ISO14443B_CMD_REQB   0x05U
#define ISO14443B_CMD_ATTRIB 0x1DU

static bool pupi_equal(const nfc_iso14443b_data_t* a, const nfc_iso14443b_data_t* b)
{
    return (memcmp(a->pupi, b->pupi, sizeof(a->pupi)) == 0);
}

static bool iso14443b_parse_atqb(const uint8_t* rx, int len, nfc_iso14443b_data_t* out)
{
    if (!rx || !out || len < 11) return false;
    memcpy(out->pupi, &rx[0], 4);
    memcpy(out->app_data, &rx[4], 4);
    memcpy(out->prot_info, &rx[8], 3);
    return true;
}

/* ISO-DEP PCB helpers (same as 14443-4A). */
#define ISO_DEP_PCB_I_BLOCK   0x02U
#define ISO_DEP_PCB_I_CHAIN   0x10U
#define ISO_DEP_PCB_R_ACK     0xA2U
#define ISO_DEP_PCB_S_WTX     0xF2U

static uint16_t iso14443b_crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x0001U) crc = (crc >> 1) ^ 0x8408U;
            else              crc >>= 1;
        }
    }
    return (uint16_t)~crc;
}

static bool iso14443b_check_crc(const uint8_t* data, size_t len)
{
    if (!data || len < 2) return false;
    uint16_t calc = iso14443b_crc16(data, len - 2);
    uint16_t rx = (uint16_t)data[len - 2] | ((uint16_t)data[len - 1] << 8);
    return (calc == rx);
}

static int iso14443b_strip_crc(uint8_t* buf, int len)
{
    if (len >= 3 && iso14443b_check_crc(buf, (size_t)len)) return len - 2;
    return len;
}

static size_t iso_dep_fsc_from_fsci(uint8_t fsci)
{
    static const uint16_t tbl[] = { 16, 24, 32, 40, 48, 64, 96, 128, 256 };
    if (fsci > 8) fsci = 8;
    return tbl[fsci];
}

static int iso_dep_fwt_ms(uint8_t fwi)
{
    uint32_t us = 302U * (1U << (fwi & 0x0FU));
    int ms = (int)((us + 999U) / 1000U);
    if (ms < 5) ms = 5;
    return ms;
}

static size_t iso14443b_fsc_from_atqb(const nfc_iso14443b_data_t* card)
{
    if (!card) return 32;
    uint8_t fsci = (uint8_t)((card->prot_info[0] >> 4) & 0x0FU);
    size_t fsc = iso_dep_fsc_from_fsci(fsci);
    if (fsc > 255) fsc = 255;
    return fsc ? fsc : 32;
}

static uint8_t iso14443b_fwi_from_atqb(const nfc_iso14443b_data_t* card)
{
    if (!card) return 4;
    uint8_t fwi = (uint8_t)((card->prot_info[1] >> 4) & 0x0FU);
    if (fwi == 0) fwi = 4;
    return fwi;
}

static bool iso_dep_is_i_block(uint8_t pcb) { return (pcb & 0xC0U) == 0x00U; }
static bool iso_dep_is_r_block(uint8_t pcb) { return (pcb & 0xC0U) == 0x80U; }
static bool iso_dep_is_s_block(uint8_t pcb) { return (pcb & 0xC0U) == 0xC0U; }
static bool iso_dep_is_wtx(uint8_t pcb)     { return iso_dep_is_s_block(pcb) && ((pcb & 0x0FU) == 0x02U); }

int iso14443b_tcl_transceive(const nfc_iso14443b_data_t* card,
                              const uint8_t*             tx,
                              size_t                     tx_len,
                              uint8_t*                   rx,
                              size_t                     rx_max,
                              int                        timeout_ms)
{
    if (!tx || !rx || tx_len == 0) return 0;

    size_t fsc = iso14443b_fsc_from_atqb(card);
    size_t max_inf = (fsc > 1) ? (fsc - 1U) : tx_len;
    int base_timeout = (timeout_ms > 0) ? timeout_ms
                                        : iso_dep_fwt_ms(iso14443b_fwi_from_atqb(card));

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
        rlen = iso14443b_strip_crc(rbuf, rlen);

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
            rlen = iso14443b_strip_crc(rbuf, rlen);
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
            rlen = iso14443b_strip_crc(rbuf, rlen);
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

static int iso14443b_apdu_transceive(const nfc_iso14443b_data_t* card,
                                      const uint8_t*             apdu,
                                      size_t                     apdu_len,
                                      uint8_t*                   rx,
                                      size_t                     rx_max,
                                      int                        timeout_ms)
{
    if (!apdu || !rx || apdu_len == 0) return 0;

    uint8_t cmd_buf[300];
    if (apdu_len > sizeof(cmd_buf)) return 0;
    memcpy(cmd_buf, apdu, apdu_len);

    int len = iso14443b_tcl_transceive(card, cmd_buf, apdu_len, rx, rx_max, timeout_ms);
    if (len < 2) return len;

    uint8_t sw1 = rx[len - 2];
    uint8_t sw2 = rx[len - 1];

    if (sw1 == 0x6CU && apdu_len >= 5) {
        cmd_buf[apdu_len - 1] = sw2;
        len = iso14443b_tcl_transceive(card, cmd_buf, apdu_len, rx, rx_max, timeout_ms);
        return len;
    }

    if (sw1 == 0x61U) {
        size_t out_pos = (size_t)(len - 2);
        uint8_t le = sw2 ? sw2 : 0x00;

        for (int i = 0; i < 8; i++) {
            uint8_t get_resp[5] = { 0x00, 0xC0, 0x00, 0x00, le };
            uint8_t tmp[260] = { 0 };
            int rlen = iso14443b_tcl_transceive(card, get_resp, sizeof(get_resp),
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

static hb_nfc_err_t t4t_b_apdu(const nfc_iso14443b_data_t* card,
                                const uint8_t*             apdu,
                                size_t                     apdu_len,
                                uint8_t*                   out,
                                size_t                     out_max,
                                size_t*                    out_len,
                                uint16_t*                  sw)
{
    if (!card || !apdu || apdu_len == 0 || !out) return HB_NFC_ERR_PARAM;

    int len = iso14443b_apdu_transceive(card, apdu, apdu_len, out, out_max, 0);
    if (len < 2) return HB_NFC_ERR_PROTOCOL;

    uint16_t status = (uint16_t)((out[len - 2] << 8) | out[len - 1]);
    if (sw) *sw = status;
    if (out_len) *out_len = (size_t)(len - 2);
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_b_select_ndef_app(const nfc_iso14443b_data_t* card)
{
    static const uint8_t aid[] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };
    uint8_t apdu[5 + sizeof(aid) + 1];
    size_t pos = 0;
    apdu[pos++] = 0x00;
    apdu[pos++] = 0xA4;
    apdu[pos++] = 0x04;
    apdu[pos++] = 0x00;
    apdu[pos++] = sizeof(aid);
    memcpy(&apdu[pos], aid, sizeof(aid));
    pos += sizeof(aid);
    apdu[pos++] = 0x00;

    uint8_t rsp[64] = { 0 };
    size_t rsp_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = t4t_b_apdu(card, apdu, pos, rsp, sizeof(rsp), &rsp_len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != 0x9000 && (sw & 0xFF00U) != 0x6100U) {
        ESP_LOGW(TAG, "SELECT NDEF APP failed SW=0x%04X", sw);
        return HB_NFC_ERR_PROTOCOL;
    }
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_b_select_file(const nfc_iso14443b_data_t* card, uint16_t fid)
{
    uint8_t apdu[7];
    apdu[0] = 0x00;
    apdu[1] = 0xA4;
    apdu[2] = 0x00;
    apdu[3] = 0x0C;
    apdu[4] = 0x02;
    apdu[5] = (uint8_t)((fid >> 8) & 0xFFU);
    apdu[6] = (uint8_t)(fid & 0xFFU);

    uint8_t rsp[32] = { 0 };
    size_t rsp_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = t4t_b_apdu(card, apdu, sizeof(apdu), rsp, sizeof(rsp), &rsp_len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != 0x9000) {
        ESP_LOGW(TAG, "SELECT FILE 0x%04X failed SW=0x%04X", fid, sw);
        return HB_NFC_ERR_PROTOCOL;
    }
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_b_read_binary(const nfc_iso14443b_data_t* card,
                                       uint16_t                   offset,
                                       uint8_t*                   out,
                                       size_t                     out_len)
{
    if (!out || out_len == 0 || out_len > 0xFF) return HB_NFC_ERR_PARAM;

    uint8_t apdu[5];
    apdu[0] = 0x00;
    apdu[1] = 0xB0;
    apdu[2] = (uint8_t)((offset >> 8) & 0xFFU);
    apdu[3] = (uint8_t)(offset & 0xFFU);
    apdu[4] = (uint8_t)out_len;

    uint8_t rsp[260] = { 0 };
    size_t rsp_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = t4t_b_apdu(card, apdu, sizeof(apdu), rsp, sizeof(rsp), &rsp_len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != 0x9000) {
        ESP_LOGW(TAG, "READ_BINARY failed SW=0x%04X", sw);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (rsp_len < out_len) return HB_NFC_ERR_PROTOCOL;
    memcpy(out, rsp, out_len);
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_b_read_cc(const nfc_iso14443b_data_t* card, t4t_cc_t* cc)
{
    if (!card || !cc) return HB_NFC_ERR_PARAM;
    memset(cc, 0, sizeof(*cc));

    hb_nfc_err_t err = t4t_b_select_ndef_app(card);
    if (err != HB_NFC_OK) return err;

    err = t4t_b_select_file(card, 0xE103U);
    if (err != HB_NFC_OK) return err;

    uint8_t buf[15] = { 0 };
    err = t4t_b_read_binary(card, 0x0000, buf, sizeof(buf));
    if (err != HB_NFC_OK) return err;

    cc->cc_len       = (uint16_t)((buf[0] << 8) | buf[1]);
    cc->mle          = (uint16_t)((buf[3] << 8) | buf[4]);
    cc->mlc          = (uint16_t)((buf[5] << 8) | buf[6]);
    cc->ndef_fid     = (uint16_t)((buf[9] << 8) | buf[10]);
    cc->ndef_max     = (uint16_t)((buf[11] << 8) | buf[12]);
    cc->read_access  = buf[13];
    cc->write_access = buf[14];

    ESP_LOGI(TAG, "CC: ndef_fid=0x%04X ndef_max=%u mle=%u mlc=%u",
             cc->ndef_fid, cc->ndef_max, cc->mle, cc->mlc);
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_b_read_ndef(const nfc_iso14443b_data_t* card,
                                     const t4t_cc_t*            cc,
                                     uint8_t*                  out,
                                     size_t                    out_max,
                                     size_t*                   out_len)
{
    if (!card || !cc || !out || out_max < 2) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = t4t_b_select_ndef_app(card);
    if (err != HB_NFC_OK) return err;

    if (cc->read_access != 0x00U) {
        ESP_LOGW(TAG, "NDEF read not allowed (access=0x%02X)", cc->read_access);
        return HB_NFC_ERR_PROTOCOL;
    }

    err = t4t_b_select_file(card, cc->ndef_fid);
    if (err != HB_NFC_OK) return err;

    uint8_t len_buf[2] = { 0 };
    err = t4t_b_read_binary(card, 0x0000, len_buf, sizeof(len_buf));
    if (err != HB_NFC_OK) return err;

    uint16_t ndef_len = (uint16_t)((len_buf[0] << 8) | len_buf[1]);
    if (ndef_len == 0 || ndef_len > cc->ndef_max) return HB_NFC_ERR_PROTOCOL;
    if ((size_t)ndef_len > out_max) return HB_NFC_ERR_PARAM;

    uint16_t offset = 2;
    uint16_t remaining = ndef_len;
    uint16_t mle = cc->mle ? cc->mle : 0xFFU;
    if (mle > 0xFFU) mle = 0xFFU;

    while (remaining > 0) {
        uint16_t chunk = remaining;
        if (chunk > mle) chunk = mle;
        err = t4t_b_read_binary(card, offset, &out[ndef_len - remaining], chunk);
        if (err != HB_NFC_OK) return err;
        offset += chunk;
        remaining -= chunk;
    }

    if (out_len) *out_len = ndef_len;
    return HB_NFC_OK;
}

hb_nfc_err_t iso14443b_poller_init(void)
{
    nfc_rf_config_t cfg = {
        .tech            = NFC_RF_TECH_B,
        .tx_rate         = NFC_RF_BR_106,
        .rx_rate         = NFC_RF_BR_106,
        .am_mod_percent  = 10, /* ISO-B uses ~10% ASK */
        .tx_parity       = true,
        .rx_raw_parity   = false,
        .guard_time_us   = 0,
        .fdt_min_us      = 0,
        .validate_fdt    = false,
    };
    hb_nfc_err_t err = nfc_rf_apply(&cfg);
    if (err != HB_NFC_OK) return err;

    if (!st25r_field_is_on()) {
        err = st25r_field_on();
        if (err != HB_NFC_OK) return err;
    }

    ESP_LOGI(TAG, "NFC-B poller ready (106 kbps)");
    return HB_NFC_OK;
}

hb_nfc_err_t iso14443b_reqb(uint8_t afi, uint8_t param, nfc_iso14443b_data_t* out)
{
    if (!out) return HB_NFC_ERR_PARAM;
    memset(out, 0, sizeof(*out));

    uint8_t cmd[3] = { ISO14443B_CMD_REQB, afi, param };
    uint8_t rx[32] = { 0 };
    int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, 30);
    if (len < 11) {
        if (len > 0) nfc_log_hex("ATQB partial:", rx, (size_t)len);
        return HB_NFC_ERR_NO_CARD;
    }

    /* ATQB: PUPI[4] + APP_DATA[4] + PROT_INFO[3] */
    if (!iso14443b_parse_atqb(rx, len, out)) return HB_NFC_ERR_PROTOCOL;

    ESP_LOGI(TAG, "ATQB: PUPI=%02X%02X%02X%02X",
             out->pupi[0], out->pupi[1], out->pupi[2], out->pupi[3]);
    return HB_NFC_OK;
}

hb_nfc_err_t iso14443b_attrib(const nfc_iso14443b_data_t* card, uint8_t fsdi, uint8_t cid)
{
    if (!card) return HB_NFC_ERR_PARAM;

    /*
     * ATTRIB (best-effort defaults):
     *  [0x1D][PUPI x4][Param1][Param2][Param3][Param4]
     *
     * Param1: FSDI in upper nibble (reader frame size).
     * Other params set to 0 (protocol type 0, no CID/NAD).
     */
    uint8_t cmd[1 + 4 + 4] = { 0 };
    cmd[0] = ISO14443B_CMD_ATTRIB;
    memcpy(&cmd[1], card->pupi, 4);
    cmd[5] = (uint8_t)((fsdi & 0x0FU) << 4);
    cmd[6] = 0x00U;
    cmd[7] = 0x00U;
    cmd[8] = (uint8_t)(cid & 0x0FU);

    uint8_t rx[8] = { 0 };
    int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, 30);
    if (len < 1) return HB_NFC_ERR_PROTOCOL;

    ESP_LOGI(TAG, "ATTRIB OK");
    return HB_NFC_OK;
}

hb_nfc_err_t iso14443b_select(nfc_iso14443b_data_t* out, uint8_t afi, uint8_t param)
{
    hb_nfc_err_t err = iso14443b_reqb(afi, param, out);
    if (err != HB_NFC_OK) return err;
    return iso14443b_attrib(out, 8, 0);
}

hb_nfc_err_t iso14443b_read_ndef(uint8_t afi, uint8_t param,
                                  uint8_t* out, size_t out_max, size_t* out_len)
{
    if (!out || out_max < 2) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = iso14443b_poller_init();
    if (err != HB_NFC_OK) return err;

    nfc_iso14443b_data_t card = { 0 };
    err = iso14443b_select(&card, afi, param);
    if (err != HB_NFC_OK) return err;

    t4t_cc_t cc = { 0 };
    err = t4t_b_read_cc(&card, &cc);
    if (err != HB_NFC_OK) return err;

    return t4t_b_read_ndef(&card, &cc, out, out_max, out_len);
}

int iso14443b_anticoll(uint8_t afi, uint8_t slots,
                        iso14443b_anticoll_entry_t* out, size_t max_out)
{
    if (!out || max_out == 0) return 0;

    uint8_t use_slots = (slots >= 16) ? 16 : 1;
    uint8_t param = (use_slots == 16) ? 0x08U : 0x00U; /* best-effort: N=16 when bit3 set */

    uint8_t cmd[3] = { ISO14443B_CMD_REQB, afi, param };
    uint8_t rx[32] = { 0 };

    int count = 0;
    int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, 30);
    if (len >= 11) {
        iso14443b_anticoll_entry_t ent = { 0 };
        if (iso14443b_parse_atqb(rx, len, &ent.card)) {
            ent.slot = 0;
            out[count++] = ent;
            if (count >= (int)max_out) return count;
        }
    }

    if (use_slots == 16) {
        for (uint8_t slot = 1; slot < 16; slot++) {
            uint8_t sm = (uint8_t)(slot & 0x0FU);
            memset(rx, 0, sizeof(rx));
            len = nfc_poller_transceive(&sm, 1, true, rx, sizeof(rx), 1, 30);
            if (len < 11) continue;

            iso14443b_anticoll_entry_t ent = { 0 };
            if (!iso14443b_parse_atqb(rx, len, &ent.card)) continue;

            bool dup = false;
            for (int i = 0; i < count; i++) {
                if (pupi_equal(&out[i].card, &ent.card)) { dup = true; break; }
            }
            if (dup) continue;

            ent.slot = slot;
            out[count++] = ent;
            if (count >= (int)max_out) break;
        }
    }

    return count;
}
