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
 * @file emv.c
 * @brief EMV contactless helpers (PPSE, AID select, GPO, READ RECORD).
 */
#include "emv.h"

#include <string.h>
#include "nfc_apdu.h"

#define EMV_PPSE_AID_LEN 14U

static const uint8_t k_ppse_aid[EMV_PPSE_AID_LEN] = {
    '2','P','A','Y','.','S','Y','S','.','D','D','F','0','1'
};

typedef struct {
    uint32_t tag;
    size_t   len;
    const uint8_t* value;
    size_t   tlv_len;
    bool     constructed;
} emv_tlv_t;

static bool emv_tlv_next(const uint8_t* data, size_t len,
                         size_t* pos, emv_tlv_t* out)
{
    if (!data || !pos || !out) return false;
    if (*pos >= len) return false;

    size_t start = *pos;
    size_t p = *pos;
    uint8_t first = data[p++];
    uint32_t tag = first;
    if ((first & 0x1FU) == 0x1FU) {
        int cnt = 0;
        uint8_t b = 0;
        do {
            if (p >= len) return false;
            b = data[p++];
            tag = (tag << 8) | b;
            cnt++;
        } while ((b & 0x80U) && cnt < 3);
        if (cnt >= 3 && (b & 0x80U)) return false;
    }

    if (p >= len) return false;
    uint8_t l = data[p++];
    size_t vlen = 0;
    if ((l & 0x80U) == 0) {
        vlen = l;
    } else {
        uint8_t n = (uint8_t)(l & 0x7FU);
        if (n == 1U) {
            if (p >= len) return false;
            vlen = data[p++];
        } else if (n == 2U) {
            if (p + 1U >= len) return false;
            vlen = ((size_t)data[p] << 8) | data[p + 1];
            p += 2U;
        } else {
            return false;
        }
    }

    if (p + vlen > len) return false;

    out->tag = tag;
    out->len = vlen;
    out->value = &data[p];
    out->tlv_len = (p - start) + vlen;
    out->constructed = (first & 0x20U) != 0;
    *pos = p + vlen;
    return true;
}

static bool emv_tlv_find_tag(const uint8_t* data, size_t len,
                             uint32_t tag, const uint8_t** val, size_t* vlen)
{
    size_t pos = 0;
    emv_tlv_t tlv;
    while (emv_tlv_next(data, len, &pos, &tlv)) {
        if (tlv.tag == tag) {
            if (val) *val = tlv.value;
            if (vlen) *vlen = tlv.len;
            return true;
        }
        if (tlv.constructed && tlv.len > 0) {
            if (emv_tlv_find_tag(tlv.value, tlv.len, tag, val, vlen)) return true;
        }
    }
    return false;
}

typedef struct {
    emv_app_t* apps;
    size_t max;
    size_t count;
    emv_app_t* last;
} emv_app_list_t;

static void emv_extract_cb(const emv_tlv_t* tlv, void* user)
{
    if (!tlv || !user) return;
    emv_app_list_t* list = (emv_app_list_t*)user;
    if (tlv->tag == 0x4FU && tlv->len > 0) {
        if (list->count >= list->max) return;
        emv_app_t* app = &list->apps[list->count++];
        memset(app, 0, sizeof(*app));
        size_t copy = (tlv->len > EMV_AID_MAX_LEN) ? EMV_AID_MAX_LEN : tlv->len;
        memcpy(app->aid, tlv->value, copy);
        app->aid_len = (uint8_t)copy;
        list->last = app;
        return;
    }
    if (tlv->tag == 0x50U && tlv->len > 0 && list->last) {
        size_t copy = (tlv->len > EMV_APP_LABEL_MAX) ? EMV_APP_LABEL_MAX : tlv->len;
        memcpy(list->last->label, tlv->value, copy);
        list->last->label[copy] = '\0';
    }
}

static void emv_tlv_scan(const uint8_t* data, size_t len,
                         void (*cb)(const emv_tlv_t*, void*), void* user)
{
    size_t pos = 0;
    emv_tlv_t tlv;
    while (emv_tlv_next(data, len, &pos, &tlv)) {
        if (cb) cb(&tlv, user);
        if (tlv.constructed && tlv.len > 0) {
            emv_tlv_scan(tlv.value, tlv.len, cb, user);
        }
    }
}

hb_nfc_err_t emv_select_ppse(hb_nfc_protocol_t proto,
                            const void* ctx,
                            uint8_t* fci, size_t fci_max, size_t* fci_len,
                            uint16_t* sw,
                            int timeout_ms)
{
    uint8_t apdu[32];
    size_t apdu_len = nfc_apdu_build_select_aid(apdu, sizeof(apdu),
                                                k_ppse_aid, EMV_PPSE_AID_LEN,
                                                true, 0x00);
    if (apdu_len == 0) return HB_NFC_ERR_PARAM;
    return nfc_apdu_transceive(proto, ctx, apdu, apdu_len,
                               fci, fci_max, fci_len, sw, timeout_ms);
}

size_t emv_extract_aids(const uint8_t* fci, size_t fci_len,
                        emv_app_t* out, size_t max)
{
    if (!fci || !out || max == 0) return 0;
    emv_app_list_t list = {
        .apps = out,
        .max = max,
        .count = 0,
        .last = NULL
    };
    emv_tlv_scan(fci, fci_len, emv_extract_cb, &list);
    return list.count;
}

hb_nfc_err_t emv_select_aid(hb_nfc_protocol_t proto,
                           const void* ctx,
                           const uint8_t* aid, size_t aid_len,
                           uint8_t* fci, size_t fci_max, size_t* fci_len,
                           uint16_t* sw,
                           int timeout_ms)
{
    uint8_t apdu[48];
    size_t apdu_len = nfc_apdu_build_select_aid(apdu, sizeof(apdu),
                                                aid, aid_len,
                                                true, 0x00);
    if (apdu_len == 0) return HB_NFC_ERR_PARAM;
    return nfc_apdu_transceive(proto, ctx, apdu, apdu_len,
                               fci, fci_max, fci_len, sw, timeout_ms);
}

static size_t emv_build_gpo_apdu(uint8_t* out, size_t max,
                                 const uint8_t* pdol, size_t pdol_len)
{
    if (!out || max < 6U) return 0;
    if (pdol_len > 0xFD) return 0;
    size_t lc = 2U + pdol_len;
    if (max < (8U + pdol_len)) return 0;

    size_t pos = 0;
    out[pos++] = 0x80;
    out[pos++] = 0xA8;
    out[pos++] = 0x00;
    out[pos++] = 0x00;
    out[pos++] = (uint8_t)lc;
    out[pos++] = 0x83;
    out[pos++] = (uint8_t)pdol_len;
    if (pdol_len > 0 && pdol) {
        memcpy(&out[pos], pdol, pdol_len);
        pos += pdol_len;
    }
    out[pos++] = 0x00; /* Le */
    return pos;
}

hb_nfc_err_t emv_get_processing_options(hb_nfc_protocol_t proto,
                                        const void* ctx,
                                        const uint8_t* pdol, size_t pdol_len,
                                        uint8_t* gpo, size_t gpo_max, size_t* gpo_len,
                                        uint16_t* sw,
                                        int timeout_ms)
{
    uint8_t apdu[64];
    size_t apdu_len = emv_build_gpo_apdu(apdu, sizeof(apdu), pdol, pdol_len);
    if (apdu_len == 0) return HB_NFC_ERR_PARAM;
    return nfc_apdu_transceive(proto, ctx, apdu, apdu_len,
                               gpo, gpo_max, gpo_len, sw, timeout_ms);
}

size_t emv_parse_afl(const uint8_t* gpo, size_t gpo_len,
                     emv_afl_entry_t* out, size_t max)
{
    if (!gpo || gpo_len < 2U || !out || max == 0) return 0;

    const uint8_t* afl = NULL;
    size_t afl_len = 0;

    if (gpo[0] == 0x80U) {
        size_t val_len = gpo[1];
        if (val_len + 2U > gpo_len || val_len < 2U) return 0;
        afl = &gpo[2 + 2]; /* skip AIP (2 bytes) */
        afl_len = val_len - 2U;
    } else if (gpo[0] == 0x77U) {
        if (!emv_tlv_find_tag(gpo, gpo_len, 0x94U, &afl, &afl_len)) return 0;
    } else {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i + 3U < afl_len && count < max; i += 4U) {
        emv_afl_entry_t* e = &out[count++];
        e->sfi = (uint8_t)((afl[i] >> 3) & 0x1FU);
        e->record_first = afl[i + 1];
        e->record_last = afl[i + 2];
        e->offline_auth_records = afl[i + 3];
    }
    return count;
}

hb_nfc_err_t emv_read_record(hb_nfc_protocol_t proto,
                             const void* ctx,
                             uint8_t sfi,
                             uint8_t record,
                             uint8_t* out, size_t out_max, size_t* out_len,
                             uint16_t* sw,
                             int timeout_ms)
{
    if (!out || out_max < 2U) return HB_NFC_ERR_PARAM;
    uint8_t apdu[5];
    apdu[0] = 0x00;
    apdu[1] = 0xB2;
    apdu[2] = record;
    apdu[3] = (uint8_t)((sfi << 3) | 0x04U);
    apdu[4] = 0x00; /* Le */
    return nfc_apdu_transceive(proto, ctx, apdu, sizeof(apdu),
                               out, out_max, out_len, sw, timeout_ms);
}

hb_nfc_err_t emv_read_records(hb_nfc_protocol_t proto,
                              const void* ctx,
                              const emv_afl_entry_t* afl, size_t afl_count,
                              emv_record_cb_t cb, void* user,
                              int timeout_ms)
{
    if (!afl || afl_count == 0) return HB_NFC_ERR_PARAM;

    uint8_t resp[256];
    for (size_t i = 0; i < afl_count; i++) {
        const emv_afl_entry_t* e = &afl[i];
        for (uint8_t rec = e->record_first; rec <= e->record_last; rec++) {
            size_t rlen = 0;
            uint16_t sw = 0;
            hb_nfc_err_t err = emv_read_record(proto, ctx, e->sfi, rec,
                                               resp, sizeof(resp), &rlen,
                                               &sw, timeout_ms);
            if (err != HB_NFC_OK) return err;
            if (cb) cb(e->sfi, rec, resp, rlen, user);
            if (rec == 0xFFU) break;
        }
    }
    return HB_NFC_OK;
}

hb_nfc_err_t emv_run_basic(hb_nfc_protocol_t proto,
                           const void* ctx,
                           emv_app_t* app_out,
                           emv_record_cb_t cb, void* user,
                           int timeout_ms)
{
    uint8_t fci[256];
    size_t fci_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = emv_select_ppse(proto, ctx, fci, sizeof(fci), &fci_len,
                                       &sw, timeout_ms);
    if (err != HB_NFC_OK) return err;

    emv_app_t apps[8];
    size_t app_count = emv_extract_aids(fci, fci_len, apps, 8);
    if (app_count == 0) return HB_NFC_ERR_PROTOCOL;

    emv_app_t* app = &apps[0];
    if (app_out) *app_out = *app;

    uint8_t fci_app[256];
    size_t fci_app_len = 0;
    err = emv_select_aid(proto, ctx, app->aid, app->aid_len,
                         fci_app, sizeof(fci_app), &fci_app_len,
                         &sw, timeout_ms);
    if (err != HB_NFC_OK) return err;

    uint8_t gpo[256];
    size_t gpo_len = 0;
    err = emv_get_processing_options(proto, ctx,
                                     NULL, 0,
                                     gpo, sizeof(gpo), &gpo_len,
                                     &sw, timeout_ms);
    if (err != HB_NFC_OK) return err;

    emv_afl_entry_t afl[12];
    size_t afl_count = emv_parse_afl(gpo, gpo_len, afl, 12);
    if (afl_count == 0) return HB_NFC_ERR_PROTOCOL;

    return emv_read_records(proto, ctx, afl, afl_count, cb, user, timeout_ms);
}
