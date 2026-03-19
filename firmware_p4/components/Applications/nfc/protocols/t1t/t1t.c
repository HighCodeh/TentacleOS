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
 * @file t1t.c
 * @brief NFC Forum Type 1 Tag (Topaz) protocol (Phase 4).
 *
 * Implements RID, RALL, READ, WRITE-E, WRITE-NE and Topaz-512 READ8/WRITE-E8.
 * CRC is computed in software and transmitted explicitly (CMD_TX_WO_CRC).
 */
#include "t1t.h"

#include <string.h>
#include "esp_log.h"

#include "iso14443a.h"
#include "poller.h"
#include "nfc_rf.h"
#include "nfc_common.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "st25r3916_reg.h"
#include "st25r3916_core.h"
#include "hb_nfc_spi.h"

#define TAG "t1t"

#define T1T_ATQA0              0x0CU
#define T1T_ATQA1              0x00U

#define T1T_CMD_RID            0x78U
#define T1T_CMD_RALL           0x00U
#define T1T_CMD_READ           0x01U
#define T1T_CMD_WRITE_E        0x53U
#define T1T_CMD_WRITE_NE       0x1AU
#define T1T_CMD_READ8          0x02U
#define T1T_CMD_WRITE_E8       0x54U

#define T1T_RID_RESP_LEN       8U
#define T1T_READ_RESP_LEN      4U
#define T1T_WRITE_RESP_LEN     4U
#define T1T_READ8_RESP_LEN     11U
#define T1T_WRITE8_RESP_LEN    11U
#define T1T_RALL_RESP_LEN      123U
#define T1T_RALL_DATA_LEN      (T1T_RALL_RESP_LEN - 2U)

static void t1t_set_antcl(bool enable)
{
    uint8_t v = 0;
    hb_spi_reg_read(REG_ISO14443A, &v);
    if (enable) v |= ISO14443A_ANTCL;
    else        v &= (uint8_t)~ISO14443A_ANTCL;
    hb_spi_reg_write(REG_ISO14443A, v);
}

static void t1t_fifo_read_all(uint8_t* out, size_t len)
{
    size_t off = 0;
    while (off < len) {
        size_t chunk = (len - off > 32U) ? 32U : (len - off);
        (void)st25r_fifo_read(&out[off], chunk);
        off += chunk;
    }
}

static int t1t_tx_bits(const uint8_t* tx, size_t tx_len, uint8_t last_bits,
                       uint8_t* rx, size_t rx_max, size_t rx_min, int timeout_ms)
{
    if (!tx || tx_len == 0) return 0;

    st25r_fifo_clear();

    uint16_t nbytes = (uint16_t)tx_len;
    uint8_t nbtx_bits = 0;
    if (last_bits > 0 && last_bits < 8) {
        nbtx_bits = last_bits;
        nbytes = (tx_len > 0) ? (uint16_t)(tx_len - 1U) : 0U;
    }

    st25r_set_tx_bytes(nbytes, nbtx_bits);
    if (st25r_fifo_load(tx, tx_len) != HB_NFC_OK) return 0;

    hb_spi_direct_cmd(CMD_TX_WO_CRC);
    if (!st25r_irq_wait_txe()) return 0;

    if (rx_min == 0) return (int)tx_len;

    uint16_t count = 0;
    int got = st25r_fifo_wait(rx_min, timeout_ms, &count);
    if (count < rx_min) {
        if (count > 0 && rx && rx_max > 0) {
            size_t to_read = (count > rx_max) ? rx_max : count;
            t1t_fifo_read_all(rx, to_read);
        }
        return 0;
    }

    if (!rx || rx_max == 0) return got;

    size_t to_read = (got > (int)rx_max) ? rx_max : (size_t)got;
    t1t_fifo_read_all(rx, to_read);
    return (int)to_read;
}

static int t1t_send_sequence(const uint8_t* seq, size_t seq_len,
                             uint8_t* rx, size_t rx_max, size_t rx_min, int timeout_ms)
{
    if (!seq || seq_len == 0) return 0;

    if (t1t_tx_bits(&seq[0], 1, 7, NULL, 0, 0, 0) <= 0) return 0;

    for (size_t i = 1; i < seq_len; i++) {
        if (i + 1 == seq_len) {
            return t1t_tx_bits(&seq[i], 1, 0, rx, rx_max, rx_min, timeout_ms);
        }
        if (t1t_tx_bits(&seq[i], 1, 0, NULL, 0, 0, 0) <= 0) return 0;
    }

    return 0;
}

static size_t t1t_append_crc(uint8_t* buf, size_t len, size_t max)
{
    if (len + 2U > max) return 0;
    uint8_t crc[2];
    iso14443a_crc(buf, len, crc);
    buf[len] = crc[0];
    buf[len + 1U] = crc[1];
    return len + 2U;
}

static bool t1t_uid4_valid(const t1t_tag_t* tag)
{
    return tag && tag->uid_len >= 4U;
}

hb_nfc_err_t t1t_poller_init(void)
{
    nfc_rf_config_t cfg = {
        .tech           = NFC_RF_TECH_A,
        .tx_rate        = NFC_RF_BR_106,
        .rx_rate        = NFC_RF_BR_106,
        .am_mod_percent = 0,
        .tx_parity      = true,
        .rx_raw_parity  = false,
        .guard_time_us  = 0,
        .fdt_min_us     = 0,
        .validate_fdt   = false,
    };

    hb_nfc_err_t err = nfc_rf_apply(&cfg);
    if (err != HB_NFC_OK) return err;

    if (!st25r_field_is_on()) {
        err = st25r_field_on();
        if (err != HB_NFC_OK) return err;
    }

    t1t_set_antcl(false);
    return HB_NFC_OK;
}

bool t1t_is_atqa(const uint8_t atqa[2])
{
    if (!atqa) return false;
    return (atqa[0] == T1T_ATQA0 && atqa[1] == T1T_ATQA1);
}

hb_nfc_err_t t1t_rid(t1t_tag_t* tag)
{
    if (!tag) return HB_NFC_ERR_PARAM;
    memset(tag, 0, sizeof(*tag));

    uint8_t cmd[9];
    size_t pos = 0;
    cmd[pos++] = T1T_CMD_RID;
    cmd[pos++] = 0x00U;
    cmd[pos++] = 0x00U;
    cmd[pos++] = 0x00U;
    cmd[pos++] = 0x00U;
    cmd[pos++] = 0x00U;
    cmd[pos++] = 0x00U;
    pos = t1t_append_crc(cmd, pos, sizeof(cmd));
    if (pos == 0) return HB_NFC_ERR_INTERNAL;

    uint8_t rx[16] = { 0 };
    int len = t1t_send_sequence(cmd, pos, rx, sizeof(rx), T1T_RID_RESP_LEN, 20);
    if (len < (int)T1T_RID_RESP_LEN) return HB_NFC_ERR_TIMEOUT;

    if (!iso14443a_check_crc(rx, (size_t)len)) return HB_NFC_ERR_CRC;
    tag->hr0 = rx[0];
    tag->hr1 = rx[1];

    if ((tag->hr0 & 0xF0U) != 0x10U) return HB_NFC_ERR_PROTOCOL;
    tag->is_topaz512 = ((tag->hr0 & 0x0FU) == 0x02U);

    tag->uid[0] = rx[2];
    tag->uid[1] = rx[3];
    tag->uid[2] = rx[4];
    tag->uid[3] = rx[5];
    tag->uid_len = 4U;
    return HB_NFC_OK;
}

hb_nfc_err_t t1t_select(t1t_tag_t* tag)
{
    if (!tag) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = t1t_poller_init();
    if (err != HB_NFC_OK) return err;

    uint8_t atqa[2] = { 0 };
    if (iso14443a_poller_reqa(atqa) != 2) return HB_NFC_ERR_NO_CARD;
    if (!t1t_is_atqa(atqa)) return HB_NFC_ERR_PROTOCOL;

    return t1t_rid(tag);
}

hb_nfc_err_t t1t_rall(t1t_tag_t* tag, uint8_t* out, size_t out_max, size_t* out_len)
{
    if (!tag || !out) return HB_NFC_ERR_PARAM;
    if (!t1t_uid4_valid(tag)) return HB_NFC_ERR_PARAM;
    if (out_max < T1T_RALL_DATA_LEN) return HB_NFC_ERR_PARAM;

    uint8_t cmd[16];
    size_t pos = 0;
    cmd[pos++] = T1T_CMD_RALL;
    cmd[pos++] = 0x00U;
    memcpy(&cmd[pos], tag->uid, T1T_UID4_LEN); pos += T1T_UID4_LEN;
    pos = t1t_append_crc(cmd, pos, sizeof(cmd));
    if (pos == 0) return HB_NFC_ERR_INTERNAL;

    uint8_t rx[128] = { 0 };
    int len = t1t_send_sequence(cmd, pos, rx, sizeof(rx), T1T_RALL_RESP_LEN, 50);
    if (len < (int)T1T_RALL_RESP_LEN) return HB_NFC_ERR_TIMEOUT;

    if (!iso14443a_check_crc(rx, (size_t)len)) return HB_NFC_ERR_CRC;
    size_t data_len = (size_t)len - 2U;
    if (data_len > out_max) return HB_NFC_ERR_PARAM;
    memcpy(out, rx, data_len);
    if (out_len) *out_len = data_len;

    if (data_len >= 9U) {
        memcpy(tag->uid, &out[2], T1T_UID_LEN);
        tag->uid_len = T1T_UID_LEN;
        tag->hr0 = out[0];
        tag->hr1 = out[1];
    }
    return HB_NFC_OK;
}

hb_nfc_err_t t1t_read_byte(const t1t_tag_t* tag, uint8_t addr, uint8_t* data)
{
    if (!tag || !data) return HB_NFC_ERR_PARAM;
    if (!t1t_uid4_valid(tag)) return HB_NFC_ERR_PARAM;

    uint8_t cmd[10];
    size_t pos = 0;
    cmd[pos++] = T1T_CMD_READ;
    cmd[pos++] = addr;
    memcpy(&cmd[pos], tag->uid, T1T_UID4_LEN); pos += T1T_UID4_LEN;
    pos = t1t_append_crc(cmd, pos, sizeof(cmd));
    if (pos == 0) return HB_NFC_ERR_INTERNAL;

    uint8_t rx[8] = { 0 };
    int len = t1t_send_sequence(cmd, pos, rx, sizeof(rx), T1T_READ_RESP_LEN, 20);
    if (len < (int)T1T_READ_RESP_LEN) return HB_NFC_ERR_TIMEOUT;
    if (!iso14443a_check_crc(rx, (size_t)len)) return HB_NFC_ERR_CRC;
    if (rx[0] != addr) return HB_NFC_ERR_PROTOCOL;
    *data = rx[1];
    return HB_NFC_OK;
}

hb_nfc_err_t t1t_write_e(const t1t_tag_t* tag, uint8_t addr, uint8_t data)
{
    if (!tag) return HB_NFC_ERR_PARAM;
    if (!t1t_uid4_valid(tag)) return HB_NFC_ERR_PARAM;

    uint8_t cmd[11];
    size_t pos = 0;
    cmd[pos++] = T1T_CMD_WRITE_E;
    cmd[pos++] = addr;
    cmd[pos++] = data;
    memcpy(&cmd[pos], tag->uid, T1T_UID4_LEN); pos += T1T_UID4_LEN;
    pos = t1t_append_crc(cmd, pos, sizeof(cmd));
    if (pos == 0) return HB_NFC_ERR_INTERNAL;

    uint8_t rx[8] = { 0 };
    int len = t1t_send_sequence(cmd, pos, rx, sizeof(rx), T1T_WRITE_RESP_LEN, 20);
    if (len < (int)T1T_WRITE_RESP_LEN) return HB_NFC_ERR_TIMEOUT;
    if (!iso14443a_check_crc(rx, (size_t)len)) return HB_NFC_ERR_CRC;
    if (rx[0] != addr || rx[1] != data) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

hb_nfc_err_t t1t_write_ne(const t1t_tag_t* tag, uint8_t addr, uint8_t data)
{
    if (!tag) return HB_NFC_ERR_PARAM;
    if (!t1t_uid4_valid(tag)) return HB_NFC_ERR_PARAM;

    uint8_t cmd[11];
    size_t pos = 0;
    cmd[pos++] = T1T_CMD_WRITE_NE;
    cmd[pos++] = addr;
    cmd[pos++] = data;
    memcpy(&cmd[pos], tag->uid, T1T_UID4_LEN); pos += T1T_UID4_LEN;
    pos = t1t_append_crc(cmd, pos, sizeof(cmd));
    if (pos == 0) return HB_NFC_ERR_INTERNAL;

    uint8_t rx[8] = { 0 };
    int len = t1t_send_sequence(cmd, pos, rx, sizeof(rx), T1T_WRITE_RESP_LEN, 20);
    if (len < (int)T1T_WRITE_RESP_LEN) return HB_NFC_ERR_TIMEOUT;
    if (!iso14443a_check_crc(rx, (size_t)len)) return HB_NFC_ERR_CRC;
    if (rx[0] != addr) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

hb_nfc_err_t t1t_read8(const t1t_tag_t* tag, uint8_t block, uint8_t out[8])
{
    if (!tag || !out) return HB_NFC_ERR_PARAM;
    if (!tag->is_topaz512) return HB_NFC_ERR_PARAM;
    if (!t1t_uid4_valid(tag)) return HB_NFC_ERR_PARAM;

    uint8_t cmd[18];
    size_t pos = 0;
    cmd[pos++] = T1T_CMD_READ8;
    cmd[pos++] = block;
    for (int i = 0; i < 8; i++) cmd[pos++] = 0x00U;
    memcpy(&cmd[pos], tag->uid, T1T_UID4_LEN); pos += T1T_UID4_LEN;
    pos = t1t_append_crc(cmd, pos, sizeof(cmd));
    if (pos == 0) return HB_NFC_ERR_INTERNAL;

    uint8_t rx[16] = { 0 };
    int len = t1t_send_sequence(cmd, pos, rx, sizeof(rx), T1T_READ8_RESP_LEN, 30);
    if (len < (int)T1T_READ8_RESP_LEN) return HB_NFC_ERR_TIMEOUT;
    if (!iso14443a_check_crc(rx, (size_t)len)) return HB_NFC_ERR_CRC;
    if (rx[0] != block) return HB_NFC_ERR_PROTOCOL;
    memcpy(out, &rx[1], 8);
    return HB_NFC_OK;
}

hb_nfc_err_t t1t_write_e8(const t1t_tag_t* tag, uint8_t block, const uint8_t data[8])
{
    if (!tag || !data) return HB_NFC_ERR_PARAM;
    if (!tag->is_topaz512) return HB_NFC_ERR_PARAM;
    if (!t1t_uid4_valid(tag)) return HB_NFC_ERR_PARAM;

    uint8_t cmd[18];
    size_t pos = 0;
    cmd[pos++] = T1T_CMD_WRITE_E8;
    cmd[pos++] = block;
    memcpy(&cmd[pos], data, 8); pos += 8;
    memcpy(&cmd[pos], tag->uid, T1T_UID4_LEN); pos += T1T_UID4_LEN;
    pos = t1t_append_crc(cmd, pos, sizeof(cmd));
    if (pos == 0) return HB_NFC_ERR_INTERNAL;

    uint8_t rx[16] = { 0 };
    int len = t1t_send_sequence(cmd, pos, rx, sizeof(rx), T1T_WRITE8_RESP_LEN, 30);
    if (len < (int)T1T_WRITE8_RESP_LEN) return HB_NFC_ERR_TIMEOUT;
    if (!iso14443a_check_crc(rx, (size_t)len)) return HB_NFC_ERR_CRC;
    if (rx[0] != block) return HB_NFC_ERR_PROTOCOL;
    if (memcmp(&rx[1], data, 8) != 0) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

