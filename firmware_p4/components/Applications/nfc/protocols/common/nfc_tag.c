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

#include "nfc_tag.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"

#include "poller.h"
#include "iso14443a.h"
#include "iso14443b.h"
#include "iso_dep.h"
#include "mf_ultralight.h"
#include "nfc_crypto.h"

static void nfc_tag_reset(nfc_tag_t *tag) {
  if (tag == NULL)
    return;
  memset(tag, 0, sizeof(*tag));
  tag->type = NFC_TAG_TYPE_UNKNOWN;
  tag->protocol = HB_PROTO_UNKNOWN;
  tag->sec_mode = NFC_TAG_SEC_PLAIN;
  tag->felica_service = FELICA_SC_COMMON;
}

const char *nfc_tag_type_str(nfc_tag_type_t type) {
  switch (type) {
    case NFC_TAG_TYPE_1:
      return "Type 1";
    case NFC_TAG_TYPE_2:
      return "Type 2";
    case NFC_TAG_TYPE_3:
      return "Type 3";
    case NFC_TAG_TYPE_4A:
      return "Type 4A";
    case NFC_TAG_TYPE_4B:
      return "Type 4B";
    case NFC_TAG_TYPE_6:
      return "Type 6";
    case NFC_TAG_TYPE_7:
      return "Type 7";
    case NFC_TAG_TYPE_V:
      return "Type V";
    default:
      return "Unknown";
  }
}

static void nfc_tag_set_uid(nfc_tag_t *tag, const uint8_t *uid, uint8_t uid_len) {
  if (tag == NULL || uid == NULL || uid_len == 0)
    return;
  if (uid_len > NFC_UID_MAX_LEN)
    uid_len = NFC_UID_MAX_LEN;
  memcpy(tag->uid, uid, uid_len);
  tag->uid_len = uid_len;
}

static hb_nfc_err_t nfc_tag_prepare_t4t(nfc_tag_t *tag) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;
  if (tag->dep.ats_len == 0) {
    hb_nfc_err_t err = iso_dep_rats(8, 0, &tag->dep);
    if (err != HB_NFC_OK)
      return err;
  }
  if (tag->t4t_cc.ndef_fid == 0) {
    hb_nfc_err_t err = t4t_read_cc(&tag->dep, &tag->t4t_cc);
    if (err != HB_NFC_OK)
      return err;
  }
  return HB_NFC_OK;
}

hb_nfc_err_t nfc_tag_detect(nfc_tag_t *tag) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;
  nfc_tag_reset(tag);

  t1t_tag_t t1 = {0};
  if (t1t_select(&t1) == HB_NFC_OK) {
    tag->type = NFC_TAG_TYPE_1;
    tag->protocol = HB_PROTO_ISO14443_3A;
    tag->t1t = t1;
    tag->block_size = t1.is_topaz512 ? 8U : 1U;
    nfc_tag_set_uid(tag, t1.uid, t1.uid_len ? t1.uid_len : 4);
    return HB_NFC_OK;
  }

  nfc_iso14443a_data_t a = {0};
  if (iso14443a_poller_select(&a) == HB_NFC_OK) {
    tag->protocol = HB_PROTO_ISO14443_3A;
    tag->iso14443a = a;
    nfc_tag_set_uid(tag, a.uid, a.uid_len);

    if (a.sak == 0x00) {
      tag->type = NFC_TAG_TYPE_2;
      tag->block_size = 4U;
      return HB_NFC_OK;
    }

    if (a.sak & 0x20) {
      tag->type = NFC_TAG_TYPE_4A;
      tag->block_size = 16U;
      (void)nfc_tag_prepare_t4t(tag);
      return HB_NFC_OK;
    }

    tag->type = NFC_TAG_TYPE_UNKNOWN;
    return HB_NFC_OK;
  }

  nfc_iso14443b_data_t b = {0};
  if (iso14443b_poller_init() == HB_NFC_OK && iso14443b_select(&b, 0x00, 0x00) == HB_NFC_OK) {
    tag->protocol = HB_PROTO_ISO14443_3B;
    tag->iso14443b = b;
    tag->type = NFC_TAG_TYPE_4B;
    tag->block_size = 16U;
    return HB_NFC_OK;
  }

  if (felica_poller_init() == HB_NFC_OK) {
    felica_tag_t f = {0};
    if (felica_sensf_req(FELICA_SC_COMMON, &f) == HB_NFC_OK) {
      tag->protocol = HB_PROTO_FELICA;
      tag->type = NFC_TAG_TYPE_3;
      tag->felica = f;
      tag->block_size = FELICA_BLOCK_SIZE;
      nfc_tag_set_uid(tag, f.idm, FELICA_IDM_LEN);
      return HB_NFC_OK;
    }
  }

  if (iso15693_poller_init() == HB_NFC_OK) {
    iso15693_tag_t v = {0};
    if (iso15693_inventory(&v) == HB_NFC_OK) {
      tag->protocol = HB_PROTO_ISO15693;
      tag->type = NFC_TAG_TYPE_6;
      tag->iso15693 = v;
      nfc_tag_set_uid(tag, v.uid, ISO15693_UID_LEN);
      (void)iso15693_get_system_info(&tag->iso15693);
      if (tag->iso15693.block_size == 0)
        tag->iso15693.block_size = 4;
      tag->block_size = tag->iso15693.block_size;
      return HB_NFC_OK;
    }
  }

  return HB_NFC_ERR_NO_CARD;
}

hb_nfc_err_t nfc_tag_get_uid(const nfc_tag_t *tag, uint8_t *uid, size_t *uid_len) {
  if (tag == NULL || uid == NULL || uid_len == NULL)
    return HB_NFC_ERR_PARAM;
  if (tag->uid_len == 0)
    return HB_NFC_ERR_PROTOCOL;
  memcpy(uid, tag->uid, tag->uid_len);
  *uid_len = tag->uid_len;
  return HB_NFC_OK;
}

void nfc_tag_set_felica_service(nfc_tag_t *tag, uint16_t service_code) {
  if (tag == NULL)
    return;
  tag->felica_service = service_code;
}

hb_nfc_err_t nfc_tag_mfp_auth_first(nfc_tag_t *tag, uint16_t block_addr, const uint8_t key[16]) {
  if (tag == NULL || key == NULL)
    return HB_NFC_ERR_PARAM;
  if (tag->protocol != HB_PROTO_ISO14443_3A)
    return HB_NFC_ERR_PARAM;

  hb_nfc_err_t err = mfp_poller_init(&tag->iso14443a, &tag->mfp);
  if (err != HB_NFC_OK)
    return err;

  err = mfp_sl3_auth_first(&tag->mfp, block_addr, key);
  if (err != HB_NFC_OK)
    return err;

  tag->type = NFC_TAG_TYPE_7;
  tag->block_size = 16U;
  return HB_NFC_OK;
}

hb_nfc_err_t nfc_tag_mfp_auth_nonfirst(nfc_tag_t *tag, uint16_t block_addr, const uint8_t key[16]) {
  if (tag == NULL || key == NULL)
    return HB_NFC_ERR_PARAM;
  if (tag->type != NFC_TAG_TYPE_7)
    return HB_NFC_ERR_AUTH;
  return mfp_sl3_auth_nonfirst(&tag->mfp, block_addr, key);
}

static hb_nfc_err_t nfc_tag_mfp_read(nfc_tag_t *tag,
                                     uint16_t block_addr,
                                     uint8_t blocks,
                                     uint8_t *out,
                                     size_t out_max,
                                     size_t *out_len) {
  if (tag == NULL || (tag->sec_mode != NFC_TAG_SEC_PLAIN && tag->sec_mode != NFC_TAG_SEC_MAC &&
                      tag->sec_mode != NFC_TAG_SEC_FULL))
    return HB_NFC_ERR_PARAM;
  return mfp_sl3_read(
      &tag->mfp, block_addr, blocks, tag->sec_mode == NFC_TAG_SEC_FULL, out, out_max, out_len);
}

static hb_nfc_err_t
nfc_tag_mfp_write(nfc_tag_t *tag, uint16_t block_addr, const uint8_t *data, size_t data_len) {
  if (tag == NULL || (tag->sec_mode != NFC_TAG_SEC_PLAIN && tag->sec_mode != NFC_TAG_SEC_MAC &&
                      tag->sec_mode != NFC_TAG_SEC_FULL))
    return HB_NFC_ERR_PARAM;
  return mfp_sl3_write(&tag->mfp, block_addr, tag->sec_mode == NFC_TAG_SEC_FULL, data, data_len);
}

hb_nfc_err_t nfc_tag_read_block(
    nfc_tag_t *tag, uint32_t block_num, uint8_t *out, size_t out_max, size_t *out_len) {
  if (tag == NULL || out == NULL || out_len == NULL)
    return HB_NFC_ERR_PARAM;

  switch (tag->type) {
    case NFC_TAG_TYPE_1: {
      if (tag->t1t.is_topaz512) {
        if (out_max < 8)
          return HB_NFC_ERR_PARAM;
        hb_nfc_err_t err = t1t_read8(&tag->t1t, (uint8_t)block_num, out);
        if (err != HB_NFC_OK)
          return err;
        *out_len = 8;
        return HB_NFC_OK;
      }
      if (out_max < 1)
        return HB_NFC_ERR_PARAM;
      uint8_t b = 0;
      hb_nfc_err_t err = t1t_read_byte(&tag->t1t, (uint8_t)block_num, &b);
      if (err != HB_NFC_OK)
        return err;
      out[0] = b;
      *out_len = 1;
      return HB_NFC_OK;
    }
    case NFC_TAG_TYPE_2: {
      if (out_max < 4)
        return HB_NFC_ERR_PARAM;
      uint8_t tmp[18] = {0};
      int len = mful_read_pages((uint8_t)block_num, tmp);
      if (len < 16)
        return HB_NFC_ERR_PROTOCOL;
      memcpy(out, tmp, 4);
      *out_len = 4;
      return HB_NFC_OK;
    }
    case NFC_TAG_TYPE_3: {
      if (out_max < FELICA_BLOCK_SIZE)
        return HB_NFC_ERR_PARAM;
      hb_nfc_err_t err =
          felica_read_blocks(&tag->felica, tag->felica_service, (uint8_t)block_num, 1, out);
      if (err != HB_NFC_OK)
        return err;
      *out_len = FELICA_BLOCK_SIZE;
      return HB_NFC_OK;
    }
    case NFC_TAG_TYPE_4A: {
      if (out_max < tag->block_size)
        return HB_NFC_ERR_PARAM;
      hb_nfc_err_t err = nfc_tag_prepare_t4t(tag);
      if (err != HB_NFC_OK)
        return err;
      uint16_t offset = (uint16_t)(block_num * tag->block_size);
      err = t4t_read_binary_ndef(&tag->dep, &tag->t4t_cc, offset, out, tag->block_size);
      if (err != HB_NFC_OK)
        return err;
      *out_len = tag->block_size;
      return HB_NFC_OK;
    }
    case NFC_TAG_TYPE_6:
    case NFC_TAG_TYPE_V: {
      return iso15693_read_single_block(&tag->iso15693, (uint8_t)block_num, out, out_max, out_len);
    }
    case NFC_TAG_TYPE_7: {
      return nfc_tag_mfp_read(tag, (uint16_t)block_num, 1, out, out_max, out_len);
    }
    default:
      break;
  }
  return HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t
nfc_tag_write_block(nfc_tag_t *tag, uint32_t block_num, const uint8_t *data, size_t data_len) {
  if (tag == NULL || data == NULL || data_len == 0)
    return HB_NFC_ERR_PARAM;

  switch (tag->type) {
    case NFC_TAG_TYPE_1:
      if (tag->t1t.is_topaz512) {
        if (data_len != 8)
          return HB_NFC_ERR_PARAM;
        return t1t_write_e8(&tag->t1t, (uint8_t)block_num, data);
      }
      if (data_len != 1)
        return HB_NFC_ERR_PARAM;
      return t1t_write_e(&tag->t1t, (uint8_t)block_num, data[0]);

    case NFC_TAG_TYPE_2:
      if (data_len != 4)
        return HB_NFC_ERR_PARAM;
      return mful_write_page((uint8_t)block_num, data);

    case NFC_TAG_TYPE_3:
      if (data_len != FELICA_BLOCK_SIZE)
        return HB_NFC_ERR_PARAM;
      return felica_write_blocks(&tag->felica, tag->felica_service, (uint8_t)block_num, 1, data);

    case NFC_TAG_TYPE_4A: {
      hb_nfc_err_t err = nfc_tag_prepare_t4t(tag);
      if (err != HB_NFC_OK)
        return err;
      uint16_t offset = (uint16_t)(block_num * tag->block_size);
      return t4t_update_binary_ndef(&tag->dep, &tag->t4t_cc, offset, data, data_len);
    }

    case NFC_TAG_TYPE_6:
    case NFC_TAG_TYPE_V:
      return iso15693_write_single_block(&tag->iso15693, (uint8_t)block_num, data, data_len);

    case NFC_TAG_TYPE_7:
      return nfc_tag_mfp_write(tag, (uint16_t)block_num, data, data_len);

    default:
      break;
  }
  return HB_NFC_ERR_PROTOCOL;
}
