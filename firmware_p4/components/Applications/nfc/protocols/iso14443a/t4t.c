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
#include "t4t.h"

#include <string.h>
#include "esp_log.h"
#include "iso_dep.h"

static const char *TAG = "NFC_T4T";

#define T4T_CC_FILE            0xE103U
#define T4T_CC_MIN             15U
#define T4T_CC_HEADER          7U
#define T4T_CC_TLV_LIMIT       1024U
#define T4T_CC_MAX             1024U
#define T4T_TLV_EXTENDED       0xFFU
#define T4T_TLV_TERMINATOR     0xFEU
#define T4T_SW_OK              0x9000U
#define T4T_SW_NO_FILE         0x6A82U
#define T4T_SHORT_LIMIT        0x8000U
#define T4T_ODO_LIMIT          0xFFFFFFU
#define T4T_DATA_MAX           255U
#define T4T_RESPONSE_SIZE      260U
#define T4T_NLEN_SHORT         2U
#define T4T_NLEN_EXTENDED      4U
#define T4T_NDEF_TLV           0x04U
#define T4T_ENDEF_TLV          0x06U
#define T4T_MAPPING_V3         3U
#define T4T_INS_SELECT         0xA4U
#define T4T_INS_READ           0xB0U
#define T4T_INS_READ_ODO       0xB1U
#define T4T_INS_UPDATE         0xD6U
#define T4T_INS_UPDATE_ODO     0xD7U
#define T4T_DDO                0x53U
#define T4T_ODO                0x54U
#define T4T_BER_LONG           0x81U
#define T4T_BER_SHORT_MAX      127U
#define T4T_ODO_BYTES          3U
#define T4T_DDO_OVERHEAD       3U
#define T4T_WRITE_ODO_OVERHEAD 8U

static uint32_t get_be(const uint8_t *data, size_t length) {
  uint32_t value = 0;
  for (size_t i = 0; i < length; i++)
    value = (value << 8U) | data[i];
  return value;
}

static hb_nfc_err_t exchange(const nfc_iso_dep_data_t *dep,
                             const uint8_t *cmd,
                             size_t cmd_len,
                             uint8_t *response,
                             size_t capacity,
                             size_t *out_len,
                             uint16_t *out_sw) {
  int got = iso_dep_apdu_transceive(dep, cmd, cmd_len, response, capacity, 0);
  *out_len = 0;
  *out_sw = 0;
  if (got < 2 || (size_t)got > capacity)
    return HB_NFC_ERR_PROTOCOL;
  *out_len = got - 2U;
  *out_sw = get_be(response + *out_len, 2U);
  return *out_sw == T4T_SW_OK ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

static hb_nfc_err_t select_app(const nfc_iso_dep_data_t *dep, bool *out_legacy) {
  uint8_t command[] = {0, T4T_INS_SELECT, 4, 0, 7, 0xD2, 0x76, 0, 0, 0x85, 1, 1, 0};
  uint8_t response[T4T_RESPONSE_SIZE];
  size_t length;
  uint16_t sw;
  *out_legacy = false;
  hb_nfc_err_t err =
      exchange(dep, command, sizeof(command), response, sizeof(response), &length, &sw);
  if (err == HB_NFC_OK || sw != T4T_SW_NO_FILE)
    return err;
  command[11] = 0; /* Mapping v1 has another AID and omits Le. */
  err = exchange(dep, command, sizeof(command) - 1U, response, sizeof(response), &length, &sw);
  if (err == HB_NFC_OK)
    *out_legacy = true;
  return err;
}

static hb_nfc_err_t select_file(const nfc_iso_dep_data_t *dep, uint16_t fid, bool legacy) {
  uint8_t command[] = {0, T4T_INS_SELECT, 0, legacy ? 0 : 0x0C, 2, fid >> 8U, fid};
  uint8_t response[T4T_RESPONSE_SIZE];
  size_t length;
  uint16_t sw;
  return exchange(dep, command, sizeof(command), response, sizeof(response), &length, &sw);
}

static hb_nfc_err_t read_binary(
    const nfc_iso_dep_data_t *dep, uint32_t offset, uint8_t *out, size_t count, bool extended) {
  if (count == 0 || count > T4T_DATA_MAX || offset > T4T_ODO_LIMIT ||
      count > T4T_ODO_LIMIT + 1U - offset)
    return HB_NFC_ERR_PARAM;
  uint8_t command[] = {0,
                       T4T_INS_READ,
                       offset >> 8U,
                       offset,
                       count,
                       T4T_ODO,
                       T4T_ODO_BYTES,
                       offset >> 16U,
                       offset >> 8U,
                       offset,
                       0};
  const bool odo = offset + count > T4T_SHORT_LIMIT;
  size_t cmd_len = 5U;
  if (odo) {
    if (!extended || count > T4T_DATA_MAX - T4T_DDO_OVERHEAD)
      return HB_NFC_ERR_PARAM;
    command[1] = T4T_INS_READ_ODO;
    command[2] = command[3] = 0;
    command[4] = 5U;
    command[10] = count + (count <= T4T_BER_SHORT_MAX ? 2U : T4T_DDO_OVERHEAD);
    cmd_len = sizeof(command);
  }
  uint8_t response[T4T_RESPONSE_SIZE];
  size_t length;
  uint16_t sw;
  hb_nfc_err_t err = exchange(dep, command, cmd_len, response, sizeof(response), &length, &sw);
  if (err != HB_NFC_OK)
    return err;
  size_t start = 0;
  if (odo) {
    if (length < 2U || response[0] != T4T_DDO)
      return HB_NFC_ERR_PROTOCOL;
    start = response[1] == T4T_BER_LONG ? 3U : 2U;
    if (length < start || (response[1] > T4T_BER_SHORT_MAX && response[1] != T4T_BER_LONG) ||
        response[start - 1U] != count)
      return HB_NFC_ERR_PROTOCOL;
  }
  if (length - start != count)
    return HB_NFC_ERR_PROTOCOL;
  memcpy(out, response + start, count);
  return HB_NFC_OK;
}

static hb_nfc_err_t update_binary(const nfc_iso_dep_data_t *dep,
                                  uint32_t offset,
                                  const uint8_t *data,
                                  size_t count,
                                  bool extended) {
  if (count == 0 || count > T4T_DATA_MAX || offset > T4T_ODO_LIMIT ||
      count > T4T_ODO_LIMIT + 1U - offset)
    return HB_NFC_ERR_PARAM;
  uint8_t command[T4T_RESPONSE_SIZE] = {0, T4T_INS_UPDATE, offset >> 8U, offset, count};
  size_t start = 5U;
  if (offset + count > T4T_SHORT_LIMIT) {
    if (!extended || count > T4T_DATA_MAX - T4T_WRITE_ODO_OVERHEAD)
      return HB_NFC_ERR_PARAM;
    command[1] = T4T_INS_UPDATE_ODO;
    command[2] = command[3] = 0;
    command[start++] = T4T_ODO;
    command[start++] = T4T_ODO_BYTES;
    command[start++] = offset >> 16U;
    command[start++] = offset >> 8U;
    command[start++] = offset;
    command[start++] = T4T_DDO;
    if (count > T4T_BER_SHORT_MAX)
      command[start++] = T4T_BER_LONG;
    command[start++] = count;
    command[4] = start - 5U + count;
  }
  memcpy(command + start, data, count);
  uint8_t response[T4T_RESPONSE_SIZE];
  size_t length;
  uint16_t sw;
  hb_nfc_err_t err =
      exchange(dep, command, start + count, response, sizeof(response), &length, &sw);
  return err == HB_NFC_OK && length != 0 ? HB_NFC_ERR_PROTOCOL : err;
}

static bool valid_fid(uint16_t fid) {
  return fid != 0 && fid != T4T_CC_FILE && fid != 0xE102U && fid != 0x3F00U && fid != 0x3FFFU &&
         fid != 0xFFFFU;
}

hb_nfc_err_t t4t_read_cc(const nfc_iso_dep_data_t *dep, t4t_cc_t *cc) {
  if (dep == NULL || cc == NULL)
    return HB_NFC_ERR_PARAM;
  memset(cc, 0, sizeof(*cc));
  t4t_cc_t parsed = {0};
  hb_nfc_err_t err = select_app(dep, &parsed.legacy);
  if (err != HB_NFC_OK)
    return err;
  err = select_file(dep, T4T_CC_FILE, parsed.legacy);
  if (err != HB_NFC_OK)
    return err;
  uint8_t header[T4T_CC_HEADER];
  err = read_binary(dep, 0, header, sizeof(header), false);
  if (err != HB_NFC_OK)
    return err;
  parsed.cc_len = get_be(header, 2U);
  parsed.mapping = header[2];
  parsed.mle = get_be(header + 3U, 2U);
  parsed.mlc = get_be(header + 5U, 2U);
  unsigned major = parsed.mapping >> 4U;
  if (parsed.cc_len < T4T_CC_MIN || parsed.cc_len > T4T_CC_MAX || major == 0 ||
      major > T4T_MAPPING_V3 || parsed.mle < T4T_CC_MIN || parsed.mlc == 0)
    return HB_NFC_ERR_PROTOCOL;
  uint32_t pos = T4T_CC_HEADER;
  for (unsigned tlvs = 0; pos < parsed.cc_len && tlvs < T4T_CC_TLV_LIMIT; tlvs++) {
    uint8_t tlv[2], value[8];
    err = read_binary(dep, pos++, tlv, 1, false);
    if (err != HB_NFC_OK)
      return err;
    if (tlv[0] == 0)
      continue;
    if (tlv[0] == T4T_TLV_TERMINATOR || pos >= parsed.cc_len)
      return HB_NFC_ERR_PROTOCOL;
    err = read_binary(dep, pos++, tlv + 1, 1, false);
    if (err != HB_NFC_OK)
      return err;
    uint16_t value_len = tlv[1];
    if (value_len == T4T_TLV_EXTENDED) {
      uint8_t length[2];
      if (parsed.cc_len - pos < sizeof(length))
        return HB_NFC_ERR_PROTOCOL;
      err = read_binary(dep, pos, length, sizeof(length), false);
      if (err != HB_NFC_OK)
        return err;
      value_len = get_be(length, sizeof(length));
      pos += sizeof(length);
    }
    if (value_len > parsed.cc_len - pos)
      return HB_NFC_ERR_PROTOCOL;
    if (tlv[0] == T4T_NDEF_TLV || tlv[0] == T4T_ENDEF_TLV) {
      bool extended = tlv[0] == T4T_ENDEF_TLV;
      if (value_len != (extended ? 8U : 6U) || (extended && major != T4T_MAPPING_V3))
        return HB_NFC_ERR_PROTOCOL;
      err = read_binary(dep, pos, value, value_len, false);
      if (err != HB_NFC_OK)
        return err;
      parsed.nlen_size = extended ? T4T_NLEN_EXTENDED : T4T_NLEN_SHORT;
      parsed.ndef_fid = get_be(value, 2U);
      parsed.ndef_max = get_be(value + 2U, parsed.nlen_size);
      parsed.read_access = value[2U + parsed.nlen_size];
      parsed.write_access = value[3U + parsed.nlen_size];
      if (!valid_fid(parsed.ndef_fid) || parsed.ndef_max < (uint32_t)parsed.nlen_size + 3U ||
          parsed.ndef_max == UINT32_MAX || (!extended && parsed.ndef_max > T4T_SHORT_LIMIT) ||
          (parsed.read_access != 0 && parsed.read_access < 0x80U) || parsed.read_access == 0xFFU ||
          (parsed.write_access != 0 && parsed.write_access < 0x80U))
        return HB_NFC_ERR_PROTOCOL;
      *cc = parsed;
      ESP_LOGI(TAG,
               "CC v%u file=%04X max=%lu MLe=%u MLc=%u",
               major,
               cc->ndef_fid,
               (unsigned long)cc->ndef_max,
               cc->mle,
               cc->mlc);
      return HB_NFC_OK;
    }
    pos += value_len;
  }
  return HB_NFC_ERR_PROTOCOL;
}

static hb_nfc_err_t select_ndef(const nfc_iso_dep_data_t *dep, const t4t_cc_t *cc) {
  bool legacy = false;
  hb_nfc_err_t err = select_app(dep, &legacy);
  if (err != HB_NFC_OK)
    return err;
  if (legacy != cc->legacy)
    return HB_NFC_ERR_PROTOCOL;
  return select_file(dep, cc->ndef_fid, legacy);
}

static unsigned nlen_size(const t4t_cc_t *cc) {
  return cc->nlen_size == T4T_NLEN_EXTENDED ? T4T_NLEN_EXTENDED : T4T_NLEN_SHORT;
}

hb_nfc_err_t t4t_read_ndef(const nfc_iso_dep_data_t *dep,
                           const t4t_cc_t *cc,
                           uint8_t *out,
                           size_t capacity,
                           size_t *out_len) {
  if (out_len != NULL)
    *out_len = 0;
  if (dep == NULL || cc == NULL || out == NULL || cc->mle == 0)
    return HB_NFC_ERR_PARAM;
  if (cc->read_access != 0)
    return HB_NFC_ERR_PROTOCOL;
  hb_nfc_err_t err = select_ndef(dep, cc);
  if (err != HB_NFC_OK)
    return err;
  unsigned prefix = nlen_size(cc);
  uint8_t nlen[T4T_NLEN_EXTENDED];
  err = read_binary(dep, 0, nlen, prefix, false);
  if (err != HB_NFC_OK)
    return err;
  uint32_t count = get_be(nlen, prefix);
  if (cc->ndef_max < prefix || count > cc->ndef_max - prefix)
    return HB_NFC_ERR_PROTOCOL;
  if (count > capacity || count > T4T_ODO_LIMIT + 1U - prefix)
    return HB_NFC_ERR_PARAM;
  size_t done = 0;
  while (done < count) {
    size_t chunk = count - done;
    size_t limit = cc->mle < T4T_DATA_MAX ? cc->mle : T4T_DATA_MAX;
    bool odo = prefix + done + chunk > T4T_SHORT_LIMIT;
    if (odo)
      limit = limit > T4T_DDO_OVERHEAD ? limit - T4T_DDO_OVERHEAD : 0;
    if (chunk > limit)
      chunk = limit;
    err = read_binary(dep, prefix + done, out + done, chunk, (cc->mapping >> 4U) == T4T_MAPPING_V3);
    if (err != HB_NFC_OK)
      return err;
    done += chunk;
  }
  if (out_len != NULL)
    *out_len = count;
  return HB_NFC_OK;
}

hb_nfc_err_t t4t_write_ndef(const nfc_iso_dep_data_t *dep,
                            const t4t_cc_t *cc,
                            const uint8_t *data,
                            size_t count) {
  if (dep == NULL || cc == NULL || (data == NULL && count))
    return HB_NFC_ERR_PARAM;
  unsigned prefix = nlen_size(cc);
  if (cc->ndef_max < prefix || count > cc->ndef_max - prefix ||
      count > T4T_ODO_LIMIT + 1U - prefix || cc->mlc < prefix)
    return HB_NFC_ERR_PARAM;
  if (cc->write_access != 0)
    return HB_NFC_ERR_PROTOCOL;
  hb_nfc_err_t err = select_ndef(dep, cc);
  if (err != HB_NFC_OK)
    return err;
  uint8_t nlen[T4T_NLEN_EXTENDED] = {0};
  err = update_binary(dep, 0, nlen, prefix, false);
  if (err != HB_NFC_OK)
    return err;
  size_t done = 0;
  while (done < count) {
    size_t chunk = count - done;
    size_t limit = cc->mlc < T4T_DATA_MAX ? cc->mlc : T4T_DATA_MAX;
    if (prefix + done + chunk > T4T_SHORT_LIMIT)
      limit = limit > T4T_WRITE_ODO_OVERHEAD ? limit - T4T_WRITE_ODO_OVERHEAD : 0;
    if (chunk > limit)
      chunk = limit;
    err = update_binary(
        dep, prefix + done, data + done, chunk, (cc->mapping >> 4U) == T4T_MAPPING_V3);
    if (err != HB_NFC_OK)
      return err;
    done += chunk;
  }
  for (unsigned i = 0; i < prefix; i++)
    nlen[prefix - i - 1U] = count >> (i * 8U);
  return update_binary(dep, 0, nlen, prefix, false);
}

hb_nfc_err_t t4t_read_binary_ndef(const nfc_iso_dep_data_t *dep,
                                  const t4t_cc_t *cc,
                                  uint32_t offset,
                                  uint8_t *out,
                                  size_t count) {
  if (dep == NULL || cc == NULL || out == NULL || offset > cc->ndef_max ||
      count > cc->ndef_max - offset || count > cc->mle)
    return HB_NFC_ERR_PARAM;
  if (offset + count > T4T_SHORT_LIMIT &&
      (cc->mle < T4T_DDO_OVERHEAD || count > cc->mle - T4T_DDO_OVERHEAD))
    return HB_NFC_ERR_PARAM;
  if (cc->read_access != 0)
    return HB_NFC_ERR_PROTOCOL;
  hb_nfc_err_t err = select_ndef(dep, cc);
  return err == HB_NFC_OK
             ? read_binary(dep, offset, out, count, (cc->mapping >> 4U) == T4T_MAPPING_V3)
             : err;
}

hb_nfc_err_t t4t_update_binary_ndef(const nfc_iso_dep_data_t *dep,
                                    const t4t_cc_t *cc,
                                    uint32_t offset,
                                    const uint8_t *data,
                                    size_t count) {
  if (dep == NULL || cc == NULL || data == NULL || offset > cc->ndef_max ||
      count > cc->ndef_max - offset || count > cc->mlc)
    return HB_NFC_ERR_PARAM;
  if (offset + count > T4T_SHORT_LIMIT &&
      (cc->mlc < T4T_WRITE_ODO_OVERHEAD || count > cc->mlc - T4T_WRITE_ODO_OVERHEAD))
    return HB_NFC_ERR_PARAM;
  if (cc->write_access != 0)
    return HB_NFC_ERR_PROTOCOL;
  hb_nfc_err_t err = select_ndef(dep, cc);
  return err == HB_NFC_OK
             ? update_binary(dep, offset, data, count, (cc->mapping >> 4U) == T4T_MAPPING_V3)
             : err;
}
