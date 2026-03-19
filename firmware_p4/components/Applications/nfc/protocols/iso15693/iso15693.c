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
 * @file iso15693.c
 * @brief ISO 15693 (NFC-V) reader/poller for ST25R3916.
 *
 * Uses nfc_poller_transceive() as transport. CRC is appended by the chip
 * when CMD_TX_WITH_CRC is used in NFC-V mode.
 */
#include "iso15693.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"

#include "nfc_poller.h"
#include "nfc_common.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "nfc_rf.h"

#define TAG "iso15693"

static int strip_crc_len(const uint8_t* buf, int len)
{
    if (len >= 3 && iso15693_check_crc(buf, (size_t)len)) return len - 2;
    return len;
}

static hb_nfc_err_t iso15693_stay_quiet(const iso15693_tag_t* tag)
{
    if (!tag) return HB_NFC_ERR_PARAM;

    uint8_t cmd[2 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_STAY_QUIET;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;

    uint8_t rx[8] = { 0 };
    (void)nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 0, 10);
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_poller_init(void)
{
    nfc_rf_config_t cfg = {
        .tech            = NFC_RF_TECH_V,
        .tx_rate         = NFC_RF_BR_V_HIGH,
        .rx_rate         = NFC_RF_BR_V_HIGH,
        .am_mod_percent  = 10, /* ISO15693 typically 10% ASK */
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

    ESP_LOGI(TAG, "ISO15693 poller ready (NFC-V 26.48 kbps)");
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_inventory(iso15693_tag_t* tag)
{
    if (!tag) return HB_NFC_ERR_PARAM;
    memset(tag, 0, sizeof(*tag));

    /* INVENTORY: [flags][cmd][mask_len=0] */
    uint8_t cmd[3] = {
        ISO15693_FLAGS_INVENTORY,
        ISO15693_CMD_INVENTORY,
        0x00U
    };

    uint8_t rx[32] = { 0 };
    int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, 30);
    if (len < 10) {
        if (len > 0) nfc_log_hex("INVENTORY partial:", rx, (size_t)len);
        return HB_NFC_ERR_NO_CARD;
    }

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "INVENTORY error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 10) return HB_NFC_ERR_PROTOCOL;

    tag->dsfid = rx[1];
    memcpy(tag->uid, &rx[2], ISO15693_UID_LEN);
    tag->info_valid = false;
    ESP_LOGI(TAG, "Tag found (UID=%02X%02X%02X%02X%02X%02X%02X%02X)",
             tag->uid[7], tag->uid[6], tag->uid[5], tag->uid[4],
             tag->uid[3], tag->uid[2], tag->uid[1], tag->uid[0]);
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_get_system_info(iso15693_tag_t* tag)
{
    if (!tag) return HB_NFC_ERR_PARAM;

    uint8_t cmd[2 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_GET_SYSTEM_INFO;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;

    uint8_t rx[64] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 2) {
        if (len > 0) nfc_log_hex("SYSINFO partial:", rx, (size_t)len);
        return HB_NFC_ERR_NO_CARD;
    }

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "SYSINFO error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 2 + ISO15693_UID_LEN) return HB_NFC_ERR_PROTOCOL;

    uint8_t info_flags = rx[1];
    pos = 2;

    /* UID is always present */
    memcpy(tag->uid, &rx[pos], ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;

    if (info_flags & 0x01U) { tag->dsfid = rx[pos++]; }
    if (info_flags & 0x02U) { tag->afi   = rx[pos++]; }
    if (info_flags & 0x04U) {
        tag->block_count = (uint16_t)rx[pos++] + 1U;             /* n-1 */
        tag->block_size  = (uint8_t)((rx[pos++] & 0x1FU) + 1U);   /* n-1 */
        tag->info_valid  = true;
    }
    if (info_flags & 0x08U) { tag->ic_ref = rx[pos++]; }

    ESP_LOGI(TAG, "SYSINFO: blocks=%u size=%u dsfid=0x%02X afi=0x%02X",
             tag->block_count, tag->block_size, tag->dsfid, tag->afi);
    return HB_NFC_OK;
}

int iso15693_inventory_all(iso15693_tag_t* out, size_t max_tags)
{
    if (!out || max_tags == 0) return 0;

    int count = 0;
    for (;;) {
        if (count >= (int)max_tags) break;

        iso15693_tag_t tag;
        hb_nfc_err_t err = iso15693_inventory(&tag);
        if (err != HB_NFC_OK) break;

        bool dup = false;
        for (int i = 0; i < count; i++) {
            if (memcmp(out[i].uid, tag.uid, ISO15693_UID_LEN) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            out[count++] = tag;
        }

        (void)iso15693_stay_quiet(&tag);
    }

    return count;
}

hb_nfc_err_t iso15693_read_single_block(const iso15693_tag_t* tag,
                                         uint8_t               block,
                                         uint8_t*              data,
                                         size_t                data_max,
                                         size_t*               data_len)
{
    if (!tag || !data) return HB_NFC_ERR_PARAM;
    uint8_t blk_size = tag->block_size ? tag->block_size : 4;
    if (data_max < blk_size) return HB_NFC_ERR_PARAM;

    uint8_t cmd[3 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_READ_SINGLE_BLOCK;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;
    cmd[pos++] = block;

    uint8_t rx[64] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 2) return HB_NFC_ERR_NO_CARD;

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "READ_SINGLE error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }

    if (plen < 1 + (int)blk_size) return HB_NFC_ERR_PROTOCOL;
    memcpy(data, &rx[1], blk_size);
    if (data_len) *data_len = blk_size;
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_write_single_block(const iso15693_tag_t* tag,
                                          uint8_t               block,
                                          const uint8_t*        data,
                                          size_t                data_len)
{
    if (!tag || !data) return HB_NFC_ERR_PARAM;
    uint8_t blk_size = tag->block_size ? tag->block_size : 4;
    if (data_len != blk_size) return HB_NFC_ERR_PARAM;

    uint8_t cmd[3 + ISO15693_UID_LEN + ISO15693_MAX_BLOCK_SIZE];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_WRITE_SINGLE_BLOCK;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;
    cmd[pos++] = block;
    memcpy(&cmd[pos], data, blk_size);
    pos += blk_size;

    uint8_t rx[8] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 1) return HB_NFC_ERR_NO_CARD;

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "WRITE_SINGLE error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 1) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_lock_block(const iso15693_tag_t* tag, uint8_t block)
{
    if (!tag) return HB_NFC_ERR_PARAM;

    uint8_t cmd[3 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_LOCK_BLOCK;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;
    cmd[pos++] = block;

    uint8_t rx[8] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 1) return HB_NFC_ERR_NO_CARD;

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "LOCK_BLOCK error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 1) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_read_multiple_blocks(const iso15693_tag_t* tag,
                                            uint8_t               first_block,
                                            uint8_t               count,
                                            uint8_t*              out_buf,
                                            size_t                out_max,
                                            size_t*               out_len)
{
    if (!tag || !out_buf || count == 0) return HB_NFC_ERR_PARAM;
    if (!tag->block_size) return HB_NFC_ERR_PARAM;

    size_t total = (size_t)count * tag->block_size;
    if (out_max < total) return HB_NFC_ERR_PARAM;

    uint8_t cmd[4 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_READ_MULTIPLE_BLOCKS;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;
    cmd[pos++] = first_block;
    cmd[pos++] = (uint8_t)(count - 1U); /* n-1 encoding */

    size_t rx_cap = 1 + total + 2; /* flags + data + CRC */
    uint8_t* rx = (uint8_t*)malloc(rx_cap);
    if (!rx) return HB_NFC_ERR_INTERNAL;
    memset(rx, 0, rx_cap);

    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, rx_cap, 1, 50);
    if (len < 2) { free(rx); return HB_NFC_ERR_NO_CARD; }

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "READ_MULTI error: 0x%02X", rx[1]);
        free(rx);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 1 + (int)total) { free(rx); return HB_NFC_ERR_PROTOCOL; }

    memcpy(out_buf, &rx[1], total);
    if (out_len) *out_len = total;
    free(rx);
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_write_afi(const iso15693_tag_t* tag, uint8_t afi)
{
    if (!tag) return HB_NFC_ERR_PARAM;

    uint8_t cmd[3 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_WRITE_AFI;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;
    cmd[pos++] = afi;

    uint8_t rx[8] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 1) return HB_NFC_ERR_NO_CARD;

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "WRITE_AFI error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 1) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_lock_afi(const iso15693_tag_t* tag)
{
    if (!tag) return HB_NFC_ERR_PARAM;

    uint8_t cmd[2 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_LOCK_AFI;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;

    uint8_t rx[8] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 1) return HB_NFC_ERR_NO_CARD;

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "LOCK_AFI error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 1) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_write_dsfid(const iso15693_tag_t* tag, uint8_t dsfid)
{
    if (!tag) return HB_NFC_ERR_PARAM;

    uint8_t cmd[3 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_WRITE_DSFID;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;
    cmd[pos++] = dsfid;

    uint8_t rx[8] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 1) return HB_NFC_ERR_NO_CARD;

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "WRITE_DSFID error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 1) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_lock_dsfid(const iso15693_tag_t* tag)
{
    if (!tag) return HB_NFC_ERR_PARAM;

    uint8_t cmd[2 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_LOCK_DSFID;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;

    uint8_t rx[8] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 1) return HB_NFC_ERR_NO_CARD;

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "LOCK_DSFID error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 1) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_OK;
}

hb_nfc_err_t iso15693_get_multi_block_sec(const iso15693_tag_t* tag,
                                           uint8_t               first_block,
                                           uint8_t               count,
                                           uint8_t*              out_buf,
                                           size_t                out_max,
                                           size_t*               out_len)
{
    if (!tag || !out_buf || count == 0) return HB_NFC_ERR_PARAM;
    if (out_max < count) return HB_NFC_ERR_PARAM;

    uint8_t cmd[4 + ISO15693_UID_LEN];
    int pos = 0;
    cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
    cmd[pos++] = ISO15693_CMD_GET_MULTI_BLOCK_SEC;
    memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;
    cmd[pos++] = first_block;
    cmd[pos++] = (uint8_t)(count - 1U);

    uint8_t rx[64] = { 0 };
    int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, 30);
    if (len < 2) return HB_NFC_ERR_NO_CARD;

    int plen = strip_crc_len(rx, len);
    if (rx[0] & ISO15693_RESP_ERROR) {
        ESP_LOGW(TAG, "GET_MULTI_BLOCK_SEC error: 0x%02X", rx[1]);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (plen < 1 + count) return HB_NFC_ERR_PROTOCOL;

    memcpy(out_buf, &rx[1], count);
    if (out_len) *out_len = count;
    return HB_NFC_OK;
}

void iso15693_dump_card(void)
{
    iso15693_tag_t tag = { 0 };
    hb_nfc_err_t err = iso15693_inventory(&tag);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "No ISO15693 tag found");
        return;
    }

    err = iso15693_get_system_info(&tag);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "SYSINFO failed, using default block size 4");
        tag.block_size = 4;
        tag.block_count = 16;
    }

    uint8_t blk[ISO15693_MAX_BLOCK_SIZE];
    for (uint16_t b = 0; b < tag.block_count; b++) {
        size_t got = 0;
        err = iso15693_read_single_block(&tag, (uint8_t)b, blk, sizeof(blk), &got);
        if (err != HB_NFC_OK) {
            ESP_LOGW(TAG, "READ block %u failed", (unsigned)b);
            break;
        }
        uint8_t b0 = (got > 0) ? blk[0] : 0;
        uint8_t b1 = (got > 1) ? blk[1] : 0;
        uint8_t b2 = (got > 2) ? blk[2] : 0;
        uint8_t b3 = (got > 3) ? blk[3] : 0;
        uint8_t b4 = (got > 4) ? blk[4] : 0;
        uint8_t b5 = (got > 5) ? blk[5] : 0;
        uint8_t b6 = (got > 6) ? blk[6] : 0;
        uint8_t b7 = (got > 7) ? blk[7] : 0;
        ESP_LOGI(TAG, "BLK %03u: %02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned)b, b0, b1, b2, b3, b4, b5, b6, b7);
    }
}

uint16_t iso15693_crc16(const uint8_t* data, size_t len)
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

bool iso15693_check_crc(const uint8_t* data, size_t len)
{
    if (!data || len < 2) return false;
    uint16_t calc = iso15693_crc16(data, len - 2);
    uint16_t rx = (uint16_t)data[len - 2] | ((uint16_t)data[len - 1] << 8);
    return (calc == rx);
}
