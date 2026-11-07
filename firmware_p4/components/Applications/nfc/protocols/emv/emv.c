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

#include "emv.h"

#include <string.h>

#include "esp_log.h"

#include "nfc_apdu.h"

static const char *TAG = "EMV";

#define TLV_TAG_MULTI_BYTE_MASK  0x1FU /**< Low 5 bits set = multi-byte tag */
#define TLV_TAG_CONTINUATION_BIT 0x80U /**< Bit 8 set = more tag bytes follow */
#define EMV_TLV_MAX_DEPTH        8U

#define TLV_LEN_LONG_FORM_BIT  0x80U /**< Bit 8 set = long-form length */
#define TLV_LEN_NUM_BYTES_MASK 0x7FU /**< Low 7 bits = number of length bytes */
#define TLV_CONSTRUCTED_BIT    0x20U /**< Bit 6 set = constructed (contains TLVs) */
#define TLV_TAG_MULTI_BYTE_MAX 3     /**< Max extra tag bytes before error */

#define EMV_TAG_AID          0x4FU /**< Application Identifier (AID) */
#define EMV_TAG_APP_LABEL    0x50U /**< Application Label */
#define EMV_TAG_RESP_FMT1    0x80U /**< Response Message Template Format 1 */
#define EMV_TAG_RESP_FMT2    0x77U /**< Response Message Template Format 2 */
#define EMV_TAG_CMD_TEMPLATE 0x83U /**< Command Template */
#define EMV_TAG_AFL          0x94U /**< Application File Locator */

#define EMV_CLA_PROPRIETARY   0x80U /**< CLA byte for EMV proprietary commands */
#define EMV_INS_GPO           0xA8U /**< INS byte for GET PROCESSING OPTIONS */
#define EMV_CLA_STANDARD      0x00U /**< CLA byte for standard ISO commands */
#define EMV_INS_READ_RECORD   0xB2U /**< INS byte for READ RECORD */
#define EMV_P1P2_ZERO         0x00U /**< P1/P2 zero value */
#define EMV_LE_ZERO           0x00U /**< Le = 0 (return max available) */
#define EMV_READ_REC_SFI_FLAG 0x04U /**< P2 flag: SFI-based record addressing */

#define EMV_GPO_APDU_MIN_LEN  6U    /**< Minimum GPO APDU buffer size */
#define EMV_GPO_APDU_OVERHEAD 8U    /**< GPO APDU fixed bytes (header+Lc+tag+len+Le) */
#define EMV_GPO_PDOL_MAX_LEN  0xFDU /**< Max PDOL data length */

#define EMV_AFL_ENTRY_SIZE 4U    /**< Bytes per AFL entry */
#define EMV_AFL_SFI_SHIFT  3     /**< Bits to shift for SFI extraction */
#define EMV_AFL_SFI_MASK   0x1FU /**< Mask for SFI after shift */
#define EMV_AIP_LEN        2U    /**< Application Interchange Profile length */

#define EMV_RECORD_MAX         0xFFU /**< Maximum record number */
#define EMV_RUN_BASIC_MAX_APPS 8U    /**< Max apps in emv_run_basic */
#define EMV_RUN_BASIC_MAX_AFL  12U   /**< Max AFL entries in emv_run_basic */

#define EMV_PPSE_AID_LEN 14U

#define EMV_APDU_SELECT_PPSE_SIZE 32U  /**< Buffer size for PPSE select APDU */
#define EMV_APDU_SELECT_AID_SIZE  48U  /**< Buffer size for AID select APDU */
#define EMV_APDU_GPO_SIZE         260U /**< Buffer size for GPO APDU */

#define EMV_RECORD_BUFFER_SIZE 256U /**< Buffer size for record read responses */
#define EMV_FCI_BUFFER_SIZE    256U /**< Buffer size for FCI responses */

#define EMV_RESP_TAG_INDEX  0U /**< Response message tag at index 0 */
#define EMV_RESP_LEN_INDEX  1U /**< Response message format 1 length at index 1 */
#define EMV_RESP_AIP_OFFSET 2U /**< Response message format 1 AIP data at offset 2 */

#define EMV_BYTE_SHIFT 8U /**< Bits to shift for multi-byte value construction */

#define EMV_TLV_LEN_1BYTE       1U /**< Single-byte length field */
#define EMV_TLV_LEN_2BYTE       2U /**< Double-byte length field */
#define EMV_TLV_LEN_2BYTE_CHECK 1U /**< Check for second byte in multi-byte length */

#define EMV_READ_REC_APDU_SIZE 5U /**< READ RECORD command APDU size */
#define EMV_READ_REC_CLA_INDEX 0U /**< CLA byte index */
#define EMV_READ_REC_INS_INDEX 1U /**< INS byte index */
#define EMV_READ_REC_P2_INDEX  3U /**< P2 byte index */
#define EMV_READ_REC_LE_INDEX  4U /**< Le byte index */

#define EMV_GPO_MIN_FMT1_LEN     2U /**< Minimum GPO response length for both formats */
#define EMV_GPO_TAG_LEN_OVERHEAD 2U /**< Tag and length bytes in GPO response */

#define EMV_AFL_MIN_BYTES        4U /**< Minimum bytes needed for AFL entry */
#define EMV_AFL_FIRST_REC_OFFSET 1U /**< First record offset within AFL entry */
#define EMV_AFL_LAST_REC_OFFSET  2U /**< Last record offset within AFL entry */
#define EMV_AFL_AUTH_REC_OFFSET  3U /**< Offline auth records offset within AFL entry */
#define EMV_AFL_ENTRY_BYTES_CHECK \
  (EMV_AFL_ENTRY_SIZE - 1U) /**< Max offset before reading next entry */

#define EMV_APP_FIRST_INDEX 0U /**< Index of first application in apps array */

static const uint8_t k_ppse_aid[EMV_PPSE_AID_LEN] = {
    '2', 'P', 'A', 'Y', '.', 'S', 'Y', 'S', '.', 'D', 'D', 'F', '0', '1'};

typedef struct {
  uint32_t tag;
  size_t len;
  const uint8_t *value;
  size_t tlv_len;
  bool constructed;
} emv_tlv_t;

static bool emv_tlv_next(const uint8_t *data, size_t len, size_t *pos, emv_tlv_t *out) {
  if (data == NULL || pos == NULL || out == NULL)
    return false;
  if (*pos >= len)
    return false;

  size_t start = *pos;
  size_t p = *pos;
  uint8_t first = data[p++];
  uint32_t tag = first;
  if ((first & TLV_TAG_MULTI_BYTE_MASK) == TLV_TAG_MULTI_BYTE_MASK) {
    int cnt = 0;
    uint8_t b = 0;
    do {
      if (p >= len)
        return false;
      b = data[p++];
      tag = (tag << 8) | b;
      cnt++;
    } while ((b & TLV_TAG_CONTINUATION_BIT) && cnt < TLV_TAG_MULTI_BYTE_MAX);
    if (cnt >= TLV_TAG_MULTI_BYTE_MAX && (b & TLV_TAG_CONTINUATION_BIT))
      return false;
  }

  if (p >= len)
    return false;
  uint8_t l = data[p++];
  size_t vlen = 0;
  if ((l & TLV_LEN_LONG_FORM_BIT) == 0) {
    vlen = l;
  } else {
    uint8_t n = (uint8_t)(l & TLV_LEN_NUM_BYTES_MASK);
    if (n == EMV_TLV_LEN_1BYTE) {
      if (p >= len)
        return false;
      vlen = data[p++];
    } else if (n == EMV_TLV_LEN_2BYTE) {
      if (p + EMV_TLV_LEN_2BYTE_CHECK >= len)
        return false;
      vlen = ((size_t)data[p] << EMV_BYTE_SHIFT) | data[p + 1];
      p += EMV_TLV_LEN_2BYTE;
    } else {
      return false;
    }
  }

  if (p + vlen > len)
    return false;

  out->tag = tag;
  out->len = vlen;
  out->value = &data[p];
  out->tlv_len = (p - start) + vlen;
  out->constructed = (first & TLV_CONSTRUCTED_BIT) != 0;
  *pos = p + vlen;
  return true;
}

static bool valid_tlv(const uint8_t *data, size_t length, unsigned depth) {
  if ((!data && length) || depth >= EMV_TLV_MAX_DEPTH)
    return false;
  size_t pos = 0;
  while (pos < length) {
    emv_tlv_t tlv;
    if (!emv_tlv_next(data, length, &pos, &tlv))
      return false;
    if (tlv.constructed && !valid_tlv(tlv.value, tlv.len, depth + 1))
      return false;
  }
  return true;
}

static bool
emv_tlv_find_tag(const uint8_t *data, size_t len, uint32_t tag, const uint8_t **val, size_t *vlen) {
  const uint8_t *buffers[EMV_TLV_MAX_DEPTH] = {data};
  size_t lengths[EMV_TLV_MAX_DEPTH] = {len};
  size_t positions[EMV_TLV_MAX_DEPTH] = {0};
  size_t depth = 0;
  for (;;) {
    emv_tlv_t tlv;
    if (!emv_tlv_next(buffers[depth], lengths[depth], &positions[depth], &tlv)) {
      if (depth == 0)
        return false;
      depth--;
      continue;
    }
    if (tlv.tag == tag) {
      if (val != NULL)
        *val = tlv.value;
      if (vlen != NULL)
        *vlen = tlv.len;
      return true;
    }
    if (tlv.constructed && tlv.len > 0) {
      if (depth + 1 == EMV_TLV_MAX_DEPTH)
        return false;
      depth++;
      buffers[depth] = tlv.value;
      lengths[depth] = tlv.len;
      positions[depth] = 0;
    }
  }
}

typedef struct {
  emv_app_t *apps;
  size_t max;
  size_t count;
  emv_app_t *last;
} emv_app_list_t;

static void emv_extract_cb(const emv_tlv_t *tlv, void *user) {
  if (tlv == NULL || user == NULL)
    return;
  emv_app_list_t *list = (emv_app_list_t *)user;
  if (tlv->tag != 0x61U)
    return;
  const uint8_t *aid = NULL, *label = NULL;
  size_t aid_len = 0, label_len = 0;
  if (!emv_tlv_find_tag(tlv->value, tlv->len, EMV_TAG_AID, &aid, &aid_len) || aid_len < 5 ||
      aid_len > EMV_AID_MAX_LEN || list->count >= list->max)
    return;
  for (size_t i = 0; i < list->count; i++)
    if (list->apps[i].aid_len == aid_len && !memcmp(list->apps[i].aid, aid, aid_len))
      return;
  emv_app_t *app = &list->apps[list->count++];
  memset(app, 0, sizeof(*app));
  memcpy(app->aid, aid, aid_len);
  app->aid_len = aid_len;
  if (emv_tlv_find_tag(tlv->value, tlv->len, EMV_TAG_APP_LABEL, &label, &label_len)) {
    if (label_len > EMV_APP_LABEL_MAX)
      label_len = EMV_APP_LABEL_MAX;
    for (size_t i = 0; i < label_len; i++)
      app->label[i] = label[i] >= ' ' && label[i] <= '~' ? label[i] : '?';
  }
}

static void
emv_tlv_scan(const uint8_t *data, size_t len, void (*cb)(const emv_tlv_t *, void *), void *user) {
  const uint8_t *buffers[EMV_TLV_MAX_DEPTH] = {data};
  size_t lengths[EMV_TLV_MAX_DEPTH] = {len};
  size_t positions[EMV_TLV_MAX_DEPTH] = {0};
  size_t depth = 0;
  for (;;) {
    emv_tlv_t tlv;
    if (!emv_tlv_next(buffers[depth], lengths[depth], &positions[depth], &tlv)) {
      if (depth == 0)
        return;
      depth--;
      continue;
    }
    if (cb != NULL)
      cb(&tlv, user);
    if (tlv.constructed && tlv.len > 0) {
      if (depth + 1 == EMV_TLV_MAX_DEPTH)
        return;
      depth++;
      buffers[depth] = tlv.value;
      lengths[depth] = tlv.len;
      positions[depth] = 0;
    }
  }
}

hb_nfc_err_t emv_select_ppse(hb_nfc_protocol_t proto,
                             const void *ctx,
                             uint8_t *fci,
                             size_t fci_max,
                             size_t *fci_len,
                             uint16_t *sw,
                             int timeout_ms) {
  if (!fci || !fci_len || !sw)
    return HB_NFC_ERR_PARAM;
  *fci_len = 0;
  *sw = 0;
  uint8_t apdu[EMV_APDU_SELECT_PPSE_SIZE];
  size_t apdu_len = nfc_apdu_build_select_aid((nfc_apdu_buf_t){apdu, sizeof(apdu)},
                                              (nfc_apdu_data_t){k_ppse_aid, EMV_PPSE_AID_LEN},
                                              true,
                                              EMV_P1P2_ZERO);
  if (apdu_len == 0)
    return HB_NFC_ERR_PARAM;
  nfc_apdu_channel_t ch = {.proto = proto, .ctx = ctx, .timeout_ms = timeout_ms};
  nfc_apdu_resp_t resp = {.buf = fci, .max = fci_max};
  hb_nfc_err_t err = nfc_apdu_transceive(&ch, apdu, apdu_len, &resp);
  *fci_len = resp.len;
  *sw = resp.sw;
  return err;
}

size_t emv_extract_aids(const uint8_t *fci, size_t fci_len, emv_app_t *out, size_t max) {
  if (fci == NULL || out == NULL || max == 0 || !valid_tlv(fci, fci_len, 0))
    return 0;
  emv_app_list_t list = {.apps = out, .max = max, .count = 0, .last = NULL};
  emv_tlv_scan(fci, fci_len, emv_extract_cb, &list);
  return list.count;
}

hb_nfc_err_t emv_select_aid(hb_nfc_protocol_t proto,
                            const void *ctx,
                            const uint8_t *aid,
                            size_t aid_len,
                            uint8_t *fci,
                            size_t fci_max,
                            size_t *fci_len,
                            uint16_t *sw,
                            int timeout_ms) {
  if (!aid || aid_len < 5 || aid_len > EMV_AID_MAX_LEN || !fci || !fci_len || !sw)
    return HB_NFC_ERR_PARAM;
  *fci_len = 0;
  *sw = 0;
  uint8_t apdu[EMV_APDU_SELECT_AID_SIZE];
  size_t apdu_len = nfc_apdu_build_select_aid(
      (nfc_apdu_buf_t){apdu, sizeof(apdu)}, (nfc_apdu_data_t){aid, aid_len}, true, EMV_P1P2_ZERO);
  if (apdu_len == 0)
    return HB_NFC_ERR_PARAM;
  nfc_apdu_channel_t ch = {.proto = proto, .ctx = ctx, .timeout_ms = timeout_ms};
  nfc_apdu_resp_t resp = {.buf = fci, .max = fci_max};
  hb_nfc_err_t err = nfc_apdu_transceive(&ch, apdu, apdu_len, &resp);
  *fci_len = resp.len;
  *sw = resp.sw;
  return err;
}

static size_t emv_build_gpo_apdu(uint8_t *out, size_t max, const uint8_t *pdol, size_t pdol_len) {
  if (out == NULL || max < EMV_GPO_APDU_MIN_LEN)
    return 0;
  if (pdol_len > EMV_PDOL_BYTES)
    return 0;
  if (pdol_len && !pdol)
    return 0;
  size_t length_bytes = pdol_len >= 0x80U ? 2U : 1U;
  size_t lc = 1U + length_bytes + pdol_len;
  if (max < (EMV_GPO_APDU_OVERHEAD + pdol_len + length_bytes - 1U))
    return 0;

  size_t pos = 0;
  out[pos++] = EMV_CLA_PROPRIETARY;
  out[pos++] = EMV_INS_GPO;
  out[pos++] = EMV_P1P2_ZERO;
  out[pos++] = EMV_P1P2_ZERO;
  out[pos++] = (uint8_t)lc;
  out[pos++] = EMV_TAG_CMD_TEMPLATE;
  if (length_bytes == 2U)
    out[pos++] = 0x81U;
  out[pos++] = (uint8_t)pdol_len;
  if (pdol_len > 0 && pdol) {
    memcpy(&out[pos], pdol, pdol_len);
    pos += pdol_len;
  }
  out[pos++] = EMV_LE_ZERO; /* Le */
  return pos;
}

hb_nfc_err_t emv_get_processing_options(hb_nfc_protocol_t proto,
                                        const void *ctx,
                                        const uint8_t *pdol,
                                        size_t pdol_len,
                                        uint8_t *gpo,
                                        size_t gpo_max,
                                        size_t *gpo_len,
                                        uint16_t *sw,
                                        int timeout_ms) {
  if (!gpo || !gpo_len || !sw)
    return HB_NFC_ERR_PARAM;
  *gpo_len = 0;
  *sw = 0;
  uint8_t apdu[EMV_APDU_GPO_SIZE];
  size_t apdu_len = emv_build_gpo_apdu(apdu, sizeof(apdu), pdol, pdol_len);
  if (apdu_len == 0)
    return HB_NFC_ERR_PARAM;
  nfc_apdu_channel_t ch = {.proto = proto, .ctx = ctx, .timeout_ms = timeout_ms};
  nfc_apdu_resp_t resp = {.buf = gpo, .max = gpo_max};
  hb_nfc_err_t err = nfc_apdu_transceive(&ch, apdu, apdu_len, &resp);
  *gpo_len = resp.len;
  *sw = resp.sw;
  return err;
}

size_t emv_parse_afl(const uint8_t *gpo, size_t gpo_len, emv_afl_entry_t *out, size_t max) {
  if (!gpo || !out || !max || !valid_tlv(gpo, gpo_len, 0))
    return 0;
  emv_tlv_t root;
  size_t pos = 0;
  if (!emv_tlv_next(gpo, gpo_len, &pos, &root) || pos != gpo_len)
    return 0;
  const uint8_t *afl = NULL;
  size_t afl_len = 0;
  if (root.tag == EMV_TAG_RESP_FMT1) {
    if (root.len < EMV_AIP_LEN)
      return 0;
    afl = root.value + EMV_AIP_LEN;
    afl_len = root.len - EMV_AIP_LEN;
  } else if (root.tag == EMV_TAG_RESP_FMT2) {
    if (!emv_tlv_find_tag(gpo, gpo_len, EMV_TAG_AFL, &afl, &afl_len))
      return 0;
  } else
    return 0;
  if (afl_len % EMV_AFL_ENTRY_SIZE || afl_len / EMV_AFL_ENTRY_SIZE > max)
    return 0;
  for (size_t i = 0; i < afl_len; i += EMV_AFL_ENTRY_SIZE) {
    uint8_t sfi = afl[i] >> EMV_AFL_SFI_SHIFT;
    if ((afl[i] & 7U) || !sfi || sfi > 30 || !afl[i + 1] || afl[i + 1] > afl[i + 2] ||
        afl[i + 3] > afl[i + 2] - afl[i + 1] + 1U)
      return 0;
  }

  size_t count = 0;
  for (size_t i = 0; i + 3U < afl_len && count < max; i += EMV_AFL_ENTRY_SIZE) {
    emv_afl_entry_t *e = &out[count++];
    e->sfi = (uint8_t)((afl[i] >> EMV_AFL_SFI_SHIFT) & EMV_AFL_SFI_MASK);
    e->record_first = afl[i + 1];
    e->record_last = afl[i + 2];
    e->offline_auth_records = afl[i + 3];
  }
  return count;
}

hb_nfc_err_t emv_read_record(hb_nfc_protocol_t proto,
                             const void *ctx,
                             uint8_t sfi,
                             uint8_t record,
                             uint8_t *out,
                             size_t out_max,
                             size_t *out_len,
                             uint16_t *sw,
                             int timeout_ms) {
  if (out == NULL || out_max < 2U || !out_len || !sw || sfi == 0 || sfi > 30 || !record)
    return HB_NFC_ERR_PARAM;
  uint8_t apdu[5];
  apdu[0] = EMV_CLA_STANDARD;
  apdu[1] = EMV_INS_READ_RECORD;
  apdu[2] = record;
  apdu[3] = (uint8_t)((sfi << EMV_AFL_SFI_SHIFT) | EMV_READ_REC_SFI_FLAG);
  apdu[4] = EMV_LE_ZERO; /* Le */
  nfc_apdu_channel_t ch = {.proto = proto, .ctx = ctx, .timeout_ms = timeout_ms};
  nfc_apdu_resp_t resp = {.buf = out, .max = out_max};
  hb_nfc_err_t err = nfc_apdu_transceive(&ch, apdu, sizeof(apdu), &resp);
  *out_len = resp.len;
  *sw = resp.sw;
  return err;
}

hb_nfc_err_t emv_read_records(hb_nfc_protocol_t proto,
                              const void *ctx,
                              const emv_afl_entry_t *afl,
                              size_t afl_count,
                              emv_record_cb_t cb,
                              void *user,
                              int timeout_ms) {
  if (afl == NULL || afl_count == 0)
    return HB_NFC_ERR_PARAM;

  uint8_t resp[EMV_RECORD_BUFFER_SIZE];
  for (size_t i = 0; i < afl_count; i++) {
    const emv_afl_entry_t *e = &afl[i];
    if (!e->sfi || e->sfi > 30 || !e->record_first || e->record_first > e->record_last ||
        e->offline_auth_records > e->record_last - e->record_first + 1U)
      return HB_NFC_ERR_PARAM;
    for (uint8_t rec = e->record_first; rec <= e->record_last; rec++) {
      size_t rlen = 0;
      uint16_t sw = 0;
      hb_nfc_err_t err =
          emv_read_record(proto, ctx, e->sfi, rec, resp, sizeof(resp), &rlen, &sw, timeout_ms);
      if (err != HB_NFC_OK || sw != 0x9000U)
        return err != HB_NFC_OK ? err : HB_NFC_ERR_PROTOCOL;
      if (cb)
        cb(e->sfi, rec, resp, rlen, user);
      if (rec == EMV_RECORD_MAX)
        break;
    }
  }
  return HB_NFC_OK;
}

hb_nfc_err_t emv_run_basic(hb_nfc_protocol_t proto,
                           const void *ctx,
                           emv_app_t *app_out,
                           emv_record_cb_t cb,
                           void *user,
                           int timeout_ms) {
  uint8_t fci[EMV_FCI_BUFFER_SIZE];
  size_t fci_len = 0;
  uint16_t sw = 0;
  hb_nfc_err_t err = emv_select_ppse(proto, ctx, fci, sizeof(fci), &fci_len, &sw, timeout_ms);
  if (err != HB_NFC_OK || sw != APDU_SW_OK)
    return err != HB_NFC_OK ? err : HB_NFC_ERR_PROTOCOL;

  emv_app_t apps[EMV_RUN_BASIC_MAX_APPS];
  size_t app_count = emv_extract_aids(fci, fci_len, apps, EMV_RUN_BASIC_MAX_APPS);
  if (app_count == 0)
    return HB_NFC_ERR_PROTOCOL;

  emv_app_t *app = &apps[0];
  if (app_out)
    *app_out = *app;

  uint8_t fci_app[EMV_FCI_BUFFER_SIZE];
  size_t fci_app_len = 0;
  err = emv_select_aid(
      proto, ctx, app->aid, app->aid_len, fci_app, sizeof(fci_app), &fci_app_len, &sw, timeout_ms);
  if (err != HB_NFC_OK || sw != APDU_SW_OK)
    return err != HB_NFC_OK ? err : HB_NFC_ERR_PROTOCOL;

  if (!valid_tlv(fci_app, fci_app_len, 0))
    return HB_NFC_ERR_PROTOCOL;
  const uint8_t *dol = NULL;
  size_t dol_length = 0, pdol_length = 0;
  uint8_t pdol[EMV_PDOL_BYTES];
  emv_tlv_find_tag(fci_app, fci_app_len, 0x9F38U, &dol, &dol_length);
  err = emv_prepare_pdol(dol, dol_length, pdol, sizeof(pdol), &pdol_length);
  if (err != HB_NFC_OK)
    return err;
  uint8_t gpo[EMV_FCI_BUFFER_SIZE];
  size_t gpo_len = 0;
  err = emv_get_processing_options(
      proto, ctx, pdol, pdol_length, gpo, sizeof(gpo), &gpo_len, &sw, timeout_ms);
  if (err != HB_NFC_OK || sw != APDU_SW_OK)
    return err != HB_NFC_OK ? err : HB_NFC_ERR_PROTOCOL;

  emv_afl_entry_t afl[EMV_RUN_BASIC_MAX_AFL];
  size_t afl_count = emv_parse_afl(gpo, gpo_len, afl, EMV_RUN_BASIC_MAX_AFL);
  if (afl_count == 0)
    return HB_NFC_ERR_PROTOCOL;

  return emv_read_records(proto, ctx, afl, afl_count, cb, user, timeout_ms);
}

#define EMV_SW_SUCCESS        0x9000U
#define EMV_TAG_PDOL          0x9F38U
#define EMV_TAG_TRACK2        0x57U
#define EMV_TAG_TRACK2_ALT    0x9F6BU
#define EMV_FIELD_COUNT_LIMIT 128U
#define EMV_RECORD_SFI_MAX    30U
#define EMV_NAME_SFI_FIRST    2U
#define EMV_NAME_SFI_LAST     3U
#define EMV_NAME_RECORD_LAST  5U

static const uint32_t FIELD_TAGS[EMV_FIELD_COUNT] = {0x5A,
                                                     0x82,
                                                     0x9F12,
                                                     0x50,
                                                     0x5F20,
                                                     0x5F24,
                                                     0x5F25,
                                                     0x5F28,
                                                     0x9F42,
                                                     0x9F17,
                                                     0x9F36,
                                                     0x9F13,
                                                     0x87,
                                                     0x9F4D};
static const uint8_t FIELD_SIZES[EMV_FIELD_COUNT] = {10, 2, 16, 16, 26, 3, 3, 2, 2, 1, 2, 2, 1, 2};

typedef struct {
  emv_profile_t *profile;
  bool is_valid;
} emv_decode_t;

static bool valid_pan(const uint8_t *data, size_t length) {
  if (!length || length > 10 || (length == 10 && (data[9] & 15U) != 15U))
    return false;
  for (size_t i = 0; i < length; i++) {
    if ((data[i] >> 4) > 9)
      return false;
    uint8_t low = data[i] & 15U;
    if (low > 9 && !(low == 15 && i + 1 == length))
      return false;
  }
  return true;
}

static void decode_field(const emv_tlv_t *tlv, void *context) {
  emv_decode_t *decode = context;
  emv_profile_t *profile = decode->profile;
  for (size_t field = 0; field < EMV_FIELD_COUNT; field++) {
    if (FIELD_TAGS[field] != tlv->tag && !(field == EMV_FIELD_HOLDER && tlv->tag == 0x9F0BU))
      continue;
    bool is_text = field >= EMV_FIELD_NAME && field <= EMV_FIELD_HOLDER;
    if (tlv->len > FIELD_SIZES[field] ||
        (!is_text && field != EMV_FIELD_PAN && tlv->len != FIELD_SIZES[field]) ||
        (field == EMV_FIELD_PAN && !valid_pan(tlv->value, tlv->len))) {
      decode->is_valid = false;
      return;
    }
    if (field == EMV_FIELD_HOLDER && profile->length[field] > tlv->len)
      return;
    memset(profile->value[field], 0, EMV_FIELD_BYTES);
    memcpy(profile->value[field], tlv->value, tlv->len);
    profile->length[field] = tlv->len;
    if (is_text)
      for (size_t i = 0; i < tlv->len; i++)
        if (profile->value[field][i] < ' ' || profile->value[field][i] > '~')
          profile->value[field][i] = '?';
    return;
  }
  if ((tlv->tag == EMV_TAG_TRACK2 || tlv->tag == EMV_TAG_TRACK2_ALT) &&
      !profile->length[EMV_FIELD_PAN]) {
    size_t digits = 0;
    while (digits < tlv->len * 2) {
      uint8_t nibble = (tlv->value[digits / 2] >> (digits % 2 ? 0 : 4)) & 15U;
      if (nibble == 13)
        break;
      if (nibble > 9 || digits >= 19) {
        decode->is_valid = false;
        return;
      }
      digits++;
    }
    if (!digits || digits + 5 > tlv->len * 2) {
      decode->is_valid = false;
      return;
    }
    size_t bytes = (digits + 1) / 2;
    memcpy(profile->value[EMV_FIELD_PAN], tlv->value, bytes);
    if (digits % 2)
      profile->value[EMV_FIELD_PAN][bytes - 1] |= 15U;
    profile->length[EMV_FIELD_PAN] = bytes;
    if (!profile->length[EMV_FIELD_EXPIRY]) {
      uint8_t expiry[2] = {0};
      for (size_t i = 0; i < 4; i++) {
        size_t pos = digits + 1 + i;
        uint8_t nibble = (tlv->value[pos / 2] >> (pos % 2 ? 0 : 4)) & 15U;
        if (nibble > 9) {
          decode->is_valid = false;
          return;
        }
        expiry[i / 2] |= nibble << (i % 2 ? 0 : 4);
      }
      memcpy(profile->value[EMV_FIELD_EXPIRY], expiry, sizeof(expiry));
      profile->length[EMV_FIELD_EXPIRY] = 3;
    }
  }
}

hb_nfc_err_t emv_decode_metadata(const uint8_t *data, size_t length, emv_profile_t *profile) {
  if (!data || !profile || !length || !valid_tlv(data, length, 0))
    return HB_NFC_ERR_PROTOCOL;
  uint8_t previous_values[EMV_FIELD_COUNT][EMV_FIELD_BYTES];
  uint8_t previous_lengths[EMV_FIELD_COUNT];
  memcpy(previous_values, profile->value, sizeof(previous_values));
  memcpy(previous_lengths, profile->length, sizeof(previous_lengths));
  emv_decode_t decode = {profile, true};
  emv_tlv_scan(data, length, decode_field, &decode);
  if (!decode.is_valid) {
    memcpy(profile->value, previous_values, sizeof(previous_values));
    memcpy(profile->length, previous_lengths, sizeof(previous_lengths));
    return HB_NFC_ERR_PROTOCOL;
  }
  return HB_NFC_OK;
}

static bool dol_entry(const uint8_t *dol, size_t length, size_t *pos, uint32_t *tag, size_t *size) {
  if (*pos >= length)
    return false;
  uint8_t first = dol[(*pos)++];
  *tag = first;
  if ((first & 31U) == 31U) {
    unsigned count = 0;
    uint8_t next;
    do {
      if (*pos >= length || count++ == 3)
        return false;
      next = dol[(*pos)++];
      *tag = (*tag << 8) | next;
    } while (next & 0x80U);
  }
  if (*pos >= length)
    return false;
  *size = dol[(*pos)++];
  return true;
}

hb_nfc_err_t emv_prepare_pdol(
    const uint8_t *dol, size_t length, uint8_t *out, size_t capacity, size_t *out_length) {
  if ((!dol && length) || !out || !out_length)
    return HB_NFC_ERR_PARAM;
  *out_length = 0;
  if (capacity > EMV_PDOL_BYTES)
    capacity = EMV_PDOL_BYTES;
  size_t pos = 0, used = 0;
  while (pos < length) {
    uint32_t tag;
    size_t size;
    if (!dol_entry(dol, length, &pos, &tag, &size) || size > capacity - used)
      return HB_NFC_ERR_PARAM;
    memset(out + used, 0, size);
    const uint8_t *value = NULL;
    size_t value_size = 0;
    static const uint8_t qualifiers[] = {0x79, 0, 0x40, 0x80};
    static const uint8_t info[] = {0xC8, 0x80, 0};
    static const uint8_t country[] = {1, 0x24};
    static const uint8_t amount[] = {0, 0, 0, 0x10, 0, 0};
    static const uint8_t merchant[] = {1};
    static const uint8_t date[] = {0x19, 1, 1};
    static const uint8_t random[] = {0x82, 0x3D, 0xDE, 0x7A};
    switch (tag) {
      case 0x9F66:
      case 0x9F40:
        value = qualifiers;
        value_size = sizeof(qualifiers);
        break;
      case 0x9F59:
        value = info;
        value_size = sizeof(info);
        break;
      case 0x9F1A:
      case 0x5F2A:
        value = country;
        value_size = sizeof(country);
        break;
      case 0x9F02:
        value = amount;
        value_size = sizeof(amount);
        break;
      case 0x9F58:
        value = merchant;
        value_size = sizeof(merchant);
        break;
      case 0x9A:
        value = date;
        value_size = sizeof(date);
        break;
      case 0x9F37:
        value = random;
        value_size = sizeof(random);
        break;
      default:
        break;
    }
    if (value)
      memcpy(out + used, value, size < value_size ? size : value_size);
    used += size;
  }
  *out_length = used;
  return HB_NFC_OK;
}

static hb_nfc_err_t get_data(hb_nfc_protocol_t proto,
                             const void *ctx,
                             uint16_t tag,
                             uint8_t *out,
                             size_t capacity,
                             size_t *length,
                             int timeout) {
  const uint8_t apdu[] = {0x80, 0xCA, tag >> 8, tag, 0};
  nfc_apdu_channel_t channel = {.proto = proto, .ctx = ctx, .timeout_ms = timeout};
  nfc_apdu_resp_t response = {.buf = out, .max = capacity};
  hb_nfc_err_t error = nfc_apdu_transceive(&channel, apdu, sizeof(apdu), &response);
  *length = response.len;
  return error == HB_NFC_OK && response.sw != EMV_SW_SUCCESS ? HB_NFC_ERR_PROTOCOL : error;
}

static void decode_log(emv_profile_t *profile,
                       const uint8_t *format,
                       size_t format_length,
                       const uint8_t *data,
                       size_t length) {
  if (profile->log_count == EMV_LOG_COUNT)
    return;
  static const uint32_t tags[] = {0x9F36, 0x9F02, 0x9F1A, 0x5F2A, 0x9A, 0x9F21};
  static const uint8_t sizes[] = {2, 6, 2, 2, 3, 3};
  emv_log_t log = {0};
  size_t pos = 0, offset = 0;
  while (pos < format_length) {
    uint32_t tag;
    size_t size;
    if (!dol_entry(format, format_length, &pos, &tag, &size) || size > length - offset)
      return;
    for (size_t i = 0; i < EMV_LOG_FIELDS; i++)
      if (tags[i] == tag) {
        if (size != sizes[i])
          return;
        memcpy(log.value[i], data + offset, size);
        log.length[i] = size;
      }
    offset += size;
  }
  if (offset != length)
    return;
  profile->logs[profile->log_count++] = log;
}

hb_nfc_err_t emv_read_application(hb_nfc_protocol_t proto,
                                  const void *ctx,
                                  emv_profile_t *profile,
                                  bool (*should_cancel)(void),
                                  int timeout_ms) {
  if (!profile || profile->app.aid_len < 5 || profile->app.aid_len > EMV_AID_MAX_LEN)
    return HB_NFC_ERR_PARAM;
  emv_app_t app = profile->app;
  memset(profile, 0, sizeof(*profile));
  profile->app = app;
  size_t label_length = strnlen(app.label, EMV_APP_LABEL_MAX);
  memcpy(profile->value[EMV_FIELD_LABEL], app.label, label_length);
  profile->length[EMV_FIELD_LABEL] = label_length;
  uint8_t buffer[512], pdol[EMV_PDOL_BYTES];
  size_t length = 0, pdol_length = 0;
  hb_nfc_err_t error;
#define CANCEL()                          \
  do {                                    \
    if (should_cancel && should_cancel()) \
      return ESP_ERR_INVALID_STATE;       \
  } while (0)
  CANCEL();
  error = emv_select_aid(proto,
                         ctx,
                         app.aid,
                         app.aid_len,
                         buffer,
                         sizeof(buffer),
                         &length,
                         &profile->status,
                         timeout_ms);
  if (error != HB_NFC_OK || profile->status != EMV_SW_SUCCESS)
    return error != HB_NFC_OK ? error : HB_NFC_ERR_PROTOCOL;
  if (emv_decode_metadata(buffer, length, profile) != HB_NFC_OK)
    return HB_NFC_ERR_PROTOCOL;
  const uint8_t *dol = NULL;
  size_t dol_length = 0;
  emv_tlv_find_tag(buffer, length, EMV_TAG_PDOL, &dol, &dol_length);
  error = emv_prepare_pdol(dol, dol_length, pdol, sizeof(pdol), &pdol_length);
  if (error != HB_NFC_OK)
    return error;
  CANCEL();
  error = emv_get_processing_options(
      proto, ctx, pdol, pdol_length, buffer, sizeof(buffer), &length, &profile->status, timeout_ms);
  if (error != HB_NFC_OK || profile->status != EMV_SW_SUCCESS)
    return error != HB_NFC_OK ? error : HB_NFC_ERR_PROTOCOL;
  if (emv_decode_metadata(buffer, length, profile) != HB_NFC_OK)
    return HB_NFC_ERR_PROTOCOL;
  emv_tlv_t root;
  size_t pos = 0;
  if (!emv_tlv_next(buffer, length, &pos, &root) || pos != length)
    return HB_NFC_ERR_PROTOCOL;
  const uint8_t *raw_afl = NULL;
  size_t raw_length = 0;
  if (root.tag == EMV_TAG_RESP_FMT1) {
    if (root.len < 2)
      return HB_NFC_ERR_PROTOCOL;
    memcpy(profile->value[EMV_FIELD_AIP], root.value, 2);
    profile->length[EMV_FIELD_AIP] = 2;
    raw_afl = root.value + 2;
    raw_length = root.len - 2;
  } else if (root.tag == EMV_TAG_RESP_FMT2)
    emv_tlv_find_tag(buffer, length, EMV_TAG_AFL, &raw_afl, &raw_length);
  else
    return HB_NFC_ERR_PROTOCOL;
  if (!profile->length[EMV_FIELD_AIP])
    return HB_NFC_ERR_PROTOCOL;
  emv_afl_entry_t afl[EMV_RUN_BASIC_MAX_AFL];
  size_t count = emv_parse_afl(buffer, length, afl, EMV_RUN_BASIC_MAX_AFL);
  if (raw_length && !count)
    return HB_NFC_ERR_PROTOCOL;
  (void)raw_afl;
  unsigned total = 0;
  for (size_t i = 0; i < count; i++)
    total += afl[i].record_last - afl[i].record_first + 1U;
  if (total > EMV_FIELD_COUNT_LIMIT)
    return HB_NFC_ERR_PROTOCOL;
  uint16_t name_mask = 0;
  for (size_t i = 0; i < count; i++)
    for (unsigned record = afl[i].record_first; record <= afl[i].record_last; record++) {
      CANCEL();
      error = emv_read_record(proto,
                              ctx,
                              afl[i].sfi,
                              record,
                              buffer,
                              sizeof(buffer),
                              &length,
                              &profile->status,
                              timeout_ms);
      if (error != HB_NFC_OK || profile->status != EMV_SW_SUCCESS)
        return error != HB_NFC_OK ? error : HB_NFC_ERR_PROTOCOL;
      if (emv_decode_metadata(buffer, length, profile) != HB_NFC_OK)
        return HB_NFC_ERR_PROTOCOL;
      profile->records_read++;
      if (afl[i].sfi >= EMV_NAME_SFI_FIRST && afl[i].sfi <= EMV_NAME_SFI_LAST &&
          record <= EMV_NAME_RECORD_LAST)
        name_mask |= 1U << ((afl[i].sfi - EMV_NAME_SFI_FIRST) * EMV_NAME_RECORD_LAST + record - 1);
    }
  const uint16_t extra[] = {0x9F13, 0x9F17, 0x9F36};
  for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) {
    CANCEL();
    if (get_data(proto, ctx, extra[i], buffer, sizeof(buffer), &length, timeout_ms) == HB_NFC_OK)
      (void)emv_decode_metadata(buffer, length, profile);
  }
  if (profile->length[EMV_FIELD_LOG_ENTRY]) {
    uint8_t sfi = profile->value[EMV_FIELD_LOG_ENTRY][0],
            records = profile->value[EMV_FIELD_LOG_ENTRY][1];
    CANCEL();
    if (sfi && sfi <= EMV_RECORD_SFI_MAX && records &&
        get_data(proto, ctx, 0x9F4F, buffer, sizeof(buffer), &length, timeout_ms) == HB_NFC_OK &&
        valid_tlv(buffer, length, 0)) {
      const uint8_t *format;
      size_t format_length;
      if (emv_tlv_find_tag(buffer, length, 0x9F4F, &format, &format_length) &&
          format_length <= EMV_LOG_FORMAT_BYTES) {
        uint8_t copy[EMV_LOG_FORMAT_BYTES];
        memcpy(copy, format, format_length);
        for (unsigned record = 1; record <= records && record <= EMV_LOG_COUNT; record++) {
          CANCEL();
          uint16_t sw;
          if (emv_read_record(
                  proto, ctx, sfi, record, buffer, sizeof(buffer), &length, &sw, timeout_ms) !=
                  HB_NFC_OK ||
              sw != EMV_SW_SUCCESS)
            break;
          decode_log(profile, copy, format_length, buffer, length);
        }
      }
    }
  }
  for (unsigned sfi = EMV_NAME_SFI_FIRST;
       sfi <= EMV_NAME_SFI_LAST && !profile->length[EMV_FIELD_HOLDER];
       sfi++)
    for (unsigned record = 1; record <= EMV_NAME_RECORD_LAST && !profile->length[EMV_FIELD_HOLDER];
         record++) {
      if (name_mask & (1U << ((sfi - EMV_NAME_SFI_FIRST) * EMV_NAME_RECORD_LAST + record - 1)))
        continue;
      CANCEL();
      uint16_t sw;
      if (emv_read_record(
              proto, ctx, sfi, record, buffer, sizeof(buffer), &length, &sw, timeout_ms) !=
              HB_NFC_OK ||
          sw != EMV_SW_SUCCESS)
        break;
      (void)emv_decode_metadata(buffer, length, profile);
    }
  CANCEL();
#undef CANCEL
  profile->is_complete = profile->length[EMV_FIELD_PAN] != 0;
  ESP_LOGD(TAG, "Application read: %u records", profile->records_read);
  return profile->is_complete ? HB_NFC_OK : HB_NFC_ERR_NOT_FOUND;
}
