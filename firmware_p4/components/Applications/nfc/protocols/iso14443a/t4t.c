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
/**
 * @file t4t.c
 * @brief ISO7816-4 Type 4 Tag (T4T) NDEF read/write over ISO-DEP.
 */
#include "t4t.h"

#include <string.h>

#include "esp_log.h"

#include "iso_dep.h"
#include "nfc_common.h"

static const char *TAG = "NFC_T4T";

#define T4T_AID_LEN         7
#define T4T_CC_FILE         0xE103U
#define T4T_CC_SIZE         15
#define T4T_CC_MAX_SIZE     1024U
#define T4T_CC_HEADER_SIZE  7U
#define T4T_NDEF_TLV        0x04U
#define T4T_NDEF_TLV_SIZE   6U
#define T4T_EXTENDED_LENGTH 0xFFU
#define T4T_NLEN_SIZE       2
#define T4T_APDU_HDR_SIZE   5
#define T4T_MAX_LE          0xFFU
#define T4T_BUF_SIZE        260

#define T4T_CLA            0x00U
#define T4T_INS_SELECT     0xA4U
#define T4T_INS_READ_BIN   0xB0U
#define T4T_INS_UPDATE_BIN 0xD6U
#define T4T_SW_OK          0x9000U
#define T4T_SW_MORE_MASK   0xFF00U
#define T4T_SW_MORE        0x6100U

static const uint8_t T4T_AID[T4T_AID_LEN] = {0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01};

static hb_nfc_err_t apdu_transceive(const nfc_iso_dep_data_t *dep,
                                    const uint8_t *apdu,
                                    size_t apdu_len,
                                    uint8_t *out,
                                    size_t out_max,
                                    size_t *out_len,
                                    uint16_t *sw);
static hb_nfc_err_t select_ndef_app(const nfc_iso_dep_data_t *dep);
static hb_nfc_err_t select_file(const nfc_iso_dep_data_t *dep, uint16_t fid);
static hb_nfc_err_t
read_binary(const nfc_iso_dep_data_t *dep, uint16_t offset, uint8_t *out, size_t out_len);
static hb_nfc_err_t
update_binary(const nfc_iso_dep_data_t *dep, uint16_t offset, const uint8_t *data, size_t data_len);

static hb_nfc_err_t apdu_transceive(const nfc_iso_dep_data_t *dep,
                                    const uint8_t *apdu,
                                    size_t apdu_len,
                                    uint8_t *out,
                                    size_t out_max,
                                    size_t *out_len,
                                    uint16_t *sw) {
  if (dep == NULL || apdu == NULL || apdu_len == 0 || out == NULL)
    return HB_NFC_ERR_PARAM;

  int len = iso_dep_apdu_transceive(dep, apdu, apdu_len, out, out_max, 0);
  if (len < 2)
    return HB_NFC_ERR_PROTOCOL;

  uint16_t status = (uint16_t)((out[len - 2] << 8) | out[len - 1]);
  if (sw != NULL)
    *sw = status;
  if (out_len != NULL) {
    *out_len = (size_t)(len - 2);
  }
  return HB_NFC_OK;
}

static hb_nfc_err_t select_ndef_app(const nfc_iso_dep_data_t *dep) {
  uint8_t apdu[5 + T4T_AID_LEN + 1];
  size_t pos = 0;
  apdu[pos++] = 0x00;
  apdu[pos++] = 0xA4;
  apdu[pos++] = 0x04;
  apdu[pos++] = 0x00;
  apdu[pos++] = T4T_AID_LEN;
  memcpy(&apdu[pos], T4T_AID, T4T_AID_LEN);
  pos += T4T_AID_LEN;
  apdu[pos++] = 0x00;

  uint8_t rsp[64] = {0};
  size_t rsp_len = 0;
  uint16_t sw = 0;
  hb_nfc_err_t err = apdu_transceive(dep, apdu, pos, rsp, sizeof(rsp), &rsp_len, &sw);
  if (err != HB_NFC_OK)
    return err;
  if (sw != T4T_SW_OK && (sw & T4T_SW_MORE_MASK) != T4T_SW_MORE) {
    ESP_LOGW(TAG, "SELECT NDEF APP failed SW=0x%04X", sw);
    return HB_NFC_ERR_PROTOCOL;
  }
  return HB_NFC_OK;
}

static hb_nfc_err_t select_file(const nfc_iso_dep_data_t *dep, uint16_t fid) {
  uint8_t apdu[7];
  apdu[0] = 0x00;
  apdu[1] = 0xA4;
  apdu[2] = 0x00;
  apdu[3] = 0x0C;
  apdu[4] = 0x02;
  apdu[5] = (uint8_t)((fid >> 8) & 0xFFU);
  apdu[6] = (uint8_t)(fid & 0xFFU);

  uint8_t rsp[32] = {0};
  size_t rsp_len = 0;
  uint16_t sw = 0;
  hb_nfc_err_t err = apdu_transceive(dep, apdu, sizeof(apdu), rsp, sizeof(rsp), &rsp_len, &sw);
  if (err != HB_NFC_OK)
    return err;
  if (sw != T4T_SW_OK) {
    ESP_LOGW(TAG, "SELECT FILE 0x%04X failed SW=0x%04X", fid, sw);
    return HB_NFC_ERR_PROTOCOL;
  }
  return HB_NFC_OK;
}

static hb_nfc_err_t
read_binary(const nfc_iso_dep_data_t *dep, uint16_t offset, uint8_t *out, size_t out_len) {
  if (out == NULL || out_len == 0 || out_len > 0xFF)
    return HB_NFC_ERR_PARAM;

  uint8_t apdu[5];
  apdu[0] = 0x00;
  apdu[1] = 0xB0;
  apdu[2] = (uint8_t)((offset >> 8) & 0xFFU);
  apdu[3] = (uint8_t)(offset & 0xFFU);
  apdu[4] = (uint8_t)out_len;

  uint8_t rsp[260] = {0};
  size_t rsp_len = 0;
  uint16_t sw = 0;
  hb_nfc_err_t err = apdu_transceive(dep, apdu, sizeof(apdu), rsp, sizeof(rsp), &rsp_len, &sw);
  if (err != HB_NFC_OK)
    return err;
  if (sw != T4T_SW_OK) {
    ESP_LOGW(TAG, "READ_BINARY failed SW=0x%04X", sw);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (rsp_len < out_len)
    return HB_NFC_ERR_PROTOCOL;
  memcpy(out, rsp, out_len);
  return HB_NFC_OK;
}

static hb_nfc_err_t update_binary(const nfc_iso_dep_data_t *dep,
                                  uint16_t offset,
                                  const uint8_t *data,
                                  size_t data_len) {
  if (data == NULL || data_len == 0 || data_len > 0xFF)
    return HB_NFC_ERR_PARAM;

  uint8_t apdu[5 + 0xFF];
  apdu[0] = 0x00;
  apdu[1] = 0xD6;
  apdu[2] = (uint8_t)((offset >> 8) & 0xFFU);
  apdu[3] = (uint8_t)(offset & 0xFFU);
  apdu[4] = (uint8_t)data_len;
  memcpy(&apdu[5], data, data_len);

  uint8_t rsp[16] = {0};
  size_t rsp_len = 0;
  uint16_t sw = 0;
  hb_nfc_err_t err = apdu_transceive(dep, apdu, 5 + data_len, rsp, sizeof(rsp), &rsp_len, &sw);
  if (err != HB_NFC_OK)
    return err;
  if (sw != T4T_SW_OK) {
    ESP_LOGW(TAG, "UPDATE_BINARY failed SW=0x%04X", sw);
    return HB_NFC_ERR_PROTOCOL;
  }
  return HB_NFC_OK;
}

hb_nfc_err_t t4t_read_cc(const nfc_iso_dep_data_t *dep, t4t_cc_t *cc) {
  if (dep == NULL || cc == NULL)
    return HB_NFC_ERR_PARAM;
  memset(cc, 0, sizeof(*cc));

  hb_nfc_err_t err = select_ndef_app(dep);
  if (err != HB_NFC_OK)
    return err;

  err = select_file(dep, T4T_CC_FILE);
  if (err != HB_NFC_OK)
    return err;

  uint8_t buf[T4T_CC_HEADER_SIZE] = {0};
  err = read_binary(dep, 0, buf, sizeof(buf));
  if (err != HB_NFC_OK)
    return err;
  cc->cc_len = (uint16_t)((buf[0] << 8) | buf[1]);
  cc->mle = (uint16_t)((buf[3] << 8) | buf[4]);
  cc->mlc = (uint16_t)((buf[5] << 8) | buf[6]);
  if (buf[2] != 0x20U || cc->cc_len < T4T_CC_SIZE || cc->cc_len > T4T_CC_MAX_SIZE ||
      cc->mle < T4T_NLEN_SIZE || cc->mlc < T4T_NLEN_SIZE)
    return HB_NFC_ERR_PROTOCOL;
  bool is_found = false;
  size_t offset = T4T_CC_HEADER_SIZE;
  while (offset + 2 <= cc->cc_len) {
    uint8_t tlv[2];
    err = read_binary(dep, offset, tlv, sizeof(tlv));
    if (err != HB_NFC_OK)
      return err;
    offset += sizeof(tlv);
    size_t length = tlv[1];
    if (length == T4T_EXTENDED_LENGTH) {
      uint8_t extended[2];
      if (offset + sizeof(extended) > cc->cc_len)
        return HB_NFC_ERR_PROTOCOL;
      err = read_binary(dep, offset, extended, sizeof(extended));
      if (err != HB_NFC_OK)
        return err;
      length = ((uint16_t)extended[0] << 8) | extended[1];
      offset += sizeof(extended);
    }
    if (length > cc->cc_len - offset)
      return HB_NFC_ERR_PROTOCOL;
    if (tlv[0] == T4T_NDEF_TLV) {
      if (length != T4T_NDEF_TLV_SIZE)
        return HB_NFC_ERR_PROTOCOL;
      uint8_t value[T4T_NDEF_TLV_SIZE];
      for (size_t read = 0; read < sizeof(value);) {
        size_t chunk = sizeof(value) - read;
        if (chunk > cc->mle)
          chunk = cc->mle;
        err = read_binary(dep, offset + read, value + read, chunk);
        if (err != HB_NFC_OK)
          return err;
        read += chunk;
      }
      cc->ndef_fid = ((uint16_t)value[0] << 8) | value[1];
      cc->ndef_max = ((uint16_t)value[2] << 8) | value[3];
      cc->read_access = value[4];
      cc->write_access = value[5];
      is_found = true;
      break;
    }
    offset += length;
  }
  if (!is_found || cc->ndef_max < T4T_NLEN_SIZE || cc->ndef_max > 0x8000U || !cc->ndef_fid ||
      cc->ndef_fid == T4T_CC_FILE)
    return HB_NFC_ERR_PROTOCOL;

  ESP_LOGI(TAG,
           "CC: ndef_fid=0x%04X ndef_max=%u mle=%u mlc=%u",
           cc->ndef_fid,
           cc->ndef_max,
           cc->mle,
           cc->mlc);
  return HB_NFC_OK;
}

hb_nfc_err_t t4t_read_ndef(const nfc_iso_dep_data_t *dep,
                           const t4t_cc_t *cc,
                           uint8_t *out,
                           size_t out_max,
                           size_t *out_len) {
  if (out_len != NULL)
    *out_len = 0;
  if (dep == NULL || cc == NULL || out == NULL || out_max < 2 || cc->mle < T4T_NLEN_SIZE)
    return HB_NFC_ERR_PARAM;

  hb_nfc_err_t err = select_ndef_app(dep);
  if (err != HB_NFC_OK)
    return err;

  if (cc->read_access != 0x00U) {
    ESP_LOGW(TAG, "NDEF read not allowed (access=0x%02X)", cc->read_access);
    return HB_NFC_ERR_PROTOCOL;
  }

  err = select_file(dep, cc->ndef_fid);
  if (err != HB_NFC_OK)
    return err;

  uint8_t len_buf[2] = {0};
  err = read_binary(dep, 0x0000, len_buf, sizeof(len_buf));
  if (err != HB_NFC_OK)
    return err;

  uint16_t ndef_len = (uint16_t)((len_buf[0] << 8) | len_buf[1]);
  if (cc->ndef_max < T4T_NLEN_SIZE || ndef_len > cc->ndef_max - T4T_NLEN_SIZE) {
    ESP_LOGW(TAG, "Invalid NDEF length %u", ndef_len);
    return HB_NFC_ERR_PROTOCOL;
  }
  if ((size_t)ndef_len > out_max) {
    ESP_LOGW(TAG, "NDEF too large (%u > %u)", ndef_len, (unsigned)out_max);
    return HB_NFC_ERR_PARAM;
  }

  uint16_t offset = 2;
  uint16_t remaining = ndef_len;
  uint16_t mle = (cc->mle != 0) ? cc->mle : 0xFFU;
  if (mle > 0xFFU)
    mle = 0xFFU;

  while (remaining > 0) {
    uint16_t chunk = remaining;
    if (chunk > mle)
      chunk = mle;

    err = read_binary(dep, offset, &out[ndef_len - remaining], chunk);
    if (err != HB_NFC_OK)
      return err;

    offset += chunk;
    remaining -= chunk;
  }

  if (out_len != NULL)
    *out_len = ndef_len;
  return HB_NFC_OK;
}

hb_nfc_err_t t4t_write_ndef(const nfc_iso_dep_data_t *dep,
                            const t4t_cc_t *cc,
                            const uint8_t *data,
                            size_t data_len) {
  if (dep == NULL || cc == NULL || data == NULL || cc->mlc < T4T_NLEN_SIZE)
    return HB_NFC_ERR_PARAM;

  hb_nfc_err_t err = select_ndef_app(dep);
  if (err != HB_NFC_OK)
    return err;

  if (cc->write_access != 0x00U) {
    ESP_LOGW(TAG, "NDEF write not allowed (access=0x%02X)", cc->write_access);
    return HB_NFC_ERR_PROTOCOL;
  }

  if (cc->ndef_max < T4T_NLEN_SIZE || data_len > cc->ndef_max - T4T_NLEN_SIZE) {
    ESP_LOGW(TAG, "NDEF too large (%u > %u)", (unsigned)data_len, (unsigned)cc->ndef_max);
    return HB_NFC_ERR_PARAM;
  }

  err = select_file(dep, cc->ndef_fid);
  if (err != HB_NFC_OK)
    return err;

  uint8_t zero[2] = {0x00, 0x00};
  err = update_binary(dep, 0x0000, zero, sizeof(zero));
  if (err != HB_NFC_OK)
    return err;

  uint16_t mlc = (cc->mlc != 0) ? cc->mlc : 0xFFU;
  if (mlc > 0xFFU)
    mlc = 0xFFU;

  uint16_t offset = 2;
  size_t remaining = data_len;
  while (remaining > 0) {
    uint16_t chunk = (remaining > mlc) ? mlc : (uint16_t)remaining;
    err = update_binary(dep, offset, data + (data_len - remaining), chunk);
    if (err != HB_NFC_OK)
      return err;
    offset += chunk;
    remaining -= chunk;
  }

  /* Write NLEN actual */
  uint8_t nlen[2];
  nlen[0] = (uint8_t)((data_len >> 8) & 0xFFU);
  nlen[1] = (uint8_t)(data_len & 0xFFU);
  return update_binary(dep, 0x0000, nlen, sizeof(nlen));
}

hb_nfc_err_t t4t_read_binary_ndef(const nfc_iso_dep_data_t *dep,
                                  const t4t_cc_t *cc,
                                  uint16_t offset,
                                  uint8_t *out,
                                  size_t out_len) {
  if (dep == NULL || cc == NULL || out == NULL || out_len == 0)
    return HB_NFC_ERR_PARAM;

  hb_nfc_err_t err = select_ndef_app(dep);
  if (err != HB_NFC_OK)
    return err;

  if (cc->read_access != 0x00U)
    return HB_NFC_ERR_PROTOCOL;

  err = select_file(dep, cc->ndef_fid);
  if (err != HB_NFC_OK)
    return err;

  if ((size_t)offset + out_len > (size_t)cc->ndef_max) {
    return HB_NFC_ERR_PARAM;
  }
  return read_binary(dep, offset, out, out_len);
}

hb_nfc_err_t t4t_update_binary_ndef(const nfc_iso_dep_data_t *dep,
                                    const t4t_cc_t *cc,
                                    uint16_t offset,
                                    const uint8_t *data,
                                    size_t data_len) {
  if (dep == NULL || cc == NULL || data == NULL || data_len == 0)
    return HB_NFC_ERR_PARAM;

  hb_nfc_err_t err = select_ndef_app(dep);
  if (err != HB_NFC_OK)
    return err;

  if (cc->write_access != 0x00U)
    return HB_NFC_ERR_PROTOCOL;

  err = select_file(dep, cc->ndef_fid);
  if (err != HB_NFC_OK)
    return err;

  if ((size_t)offset + data_len > (size_t)cc->ndef_max) {
    return HB_NFC_ERR_PARAM;
  }
  return update_binary(dep, offset, data, data_len);
}
