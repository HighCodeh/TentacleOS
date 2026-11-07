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

#include "nfc_plus_poller.h"

#include <string.h>

#include "esp_log.h"

#define PLUS_KEY_BASE           0x4000U
#define PLUS_CONFIG_BASE        0xB000U
#define PLUS_ADMIN_COUNT        4U
#define PLUS_MEMORY_1K          64U
#define PLUS_MEMORY_2K          128U
#define PLUS_MEMORY_4K          256U
#define PLUS_SMALL_SECTORS      32U
#define PLUS_SMALL_BLOCKS       4U
#define PLUS_LARGE_BLOCKS       16U
#define PLUS_VENDOR             0x04U
#define PLUS_FAMILY             0x02U
#define PLUS_TYPE_MASK          0x0FU
#define PLUS_VERSION_STORAGE    5U
#define PLUS_STORAGE_2K         0x16U
#define PLUS_STORAGE_4K         0x18U
#define PLUS_VERSION_MAJOR      3U
#define PLUS_EV1                0x11U
#define PLUS_EV2                0x22U
#define PLUS_ATS_MIN            2U
#define PLUS_HISTORY_SIZE       7U
#define PLUS_HISTORY_TAG        0xC1U
#define PLUS_HISTORY_LENGTH     0x05U
#define PLUS_HISTORY_TYPE       0x20U
#define PLUS_HISTORY_SE         0x30U
#define PLUS_HISTORY_SX         0x2FU
#define PLUS_ATS_INTERFACE_MASK 0x70U
#define PLUS_SL3_SAK            0x20U
#define PLUS_PROBE_TIMEOUT      500U
#define PLUS_PROBE_REJECT       0x0BU
#define PLUS_PROBE_AUTH         0x06U
#define PLUS_PROBE_SL0          0x09U
#define PLUS_SL1_2K             0x08U
#define PLUS_SL1_4K             0x18U
#define PLUS_SL2_2K             0x10U
#define PLUS_SL2_4K             0x11U
#define PLUS_LEVEL_UNKNOWN      UINT8_MAX
#define PLUS_ATS_T0             0x46U
#define PLUS_ATS_TC1            0x02U

static const char *TAG = "NFC_PLUS_POLLER";
static const uint16_t ADMIN_KEYS[PLUS_ADMIN_COUNT] = {0x9000U, 0x9001U, 0x9003U, 0x9004U};

static uint16_t first_block(unsigned sector) {
  return sector < PLUS_SMALL_SECTORS
             ? sector * PLUS_SMALL_BLOCKS
             : PLUS_MEMORY_2K + (sector - PLUS_SMALL_SECTORS) * PLUS_LARGE_BLOCKS;
}

static unsigned sector_count(const nfc_plus_file_t *card) {
  return card->block_count <= PLUS_MEMORY_2K
             ? card->block_count / PLUS_SMALL_BLOCKS
             : PLUS_SMALL_SECTORS + (card->block_count - PLUS_MEMORY_2K) / PLUS_LARGE_BLOCKS;
}

static bool cancelled(nfc_plus_poller_cancel_cb_t cancel) {
  return cancel != NULL && cancel();
}

static const char *ats_type(const mfp_session_t *session) {
  if (session->dep.ats_len < PLUS_ATS_MIN)
    return NULL;
  size_t offset = PLUS_ATS_MIN;
  uint8_t interfaces = session->dep.ats[1] & PLUS_ATS_INTERFACE_MASK;
  for (; interfaces; interfaces &= interfaces - 1)
    offset++;
  if (session->dep.ats_len != offset + PLUS_HISTORY_SIZE)
    return NULL;
  const uint8_t *history = session->dep.ats + offset;
  if (history[0] != PLUS_HISTORY_TAG || history[1] != PLUS_HISTORY_LENGTH ||
      (history[2] & ~PLUS_TYPE_MASK) != PLUS_HISTORY_TYPE)
    return NULL;
  if (history[3] == PLUS_HISTORY_SE && (history[2] & PLUS_TYPE_MASK) <= 1)
    return "Mifare Plus SE";
  if (history[2] == PLUS_HISTORY_SX && history[3] == PLUS_HISTORY_SX)
    return history[4] & 1U ? "Mifare Plus X" : "Mifare Plus S";
  return NULL;
}

static hb_nfc_err_t identify(const nfc_iso14443a_data_t *identity,
                             mfp_session_t *session,
                             nfc_plus_file_t *card,
                             uint8_t *out_level) {
  if (identity->uid_len != 4U && identity->uid_len != 7U)
    return HB_NFC_ERR_PROTOCOL;
  hb_nfc_err_t err = mfp_poller_init(identity, session);
  if (err != HB_NFC_OK)
    return err;
  bool version = mfp_get_version(session, card->version) == HB_NFC_OK &&
                 card->version[0] == PLUS_VENDOR &&
                 (card->version[1] & PLUS_TYPE_MASK) == PLUS_FAMILY;
  const char *type = ats_type(session);
  /* An invalid personalization address probes SL without changing a card. */
  const uint8_t probe[19] = {0xA8U, 0xFFU, 0xFFU};
  uint8_t response[4];
  int length =
      identity->sak == PLUS_SL3_SAK
          ? mfp_apdu_transceive(
                session, probe, sizeof(probe), response, sizeof(response), PLUS_PROBE_TIMEOUT)
          : 0;
  bool sl3 = length == 1 && (response[0] == PLUS_PROBE_REJECT || response[0] == PLUS_PROBE_AUTH);
  bool sl0 = length == 1 && response[0] == PLUS_PROBE_SL0;
  bool sl2 = identity->sak == PLUS_SL2_2K || identity->sak == PLUS_SL2_4K;
  if (!version && type == NULL && !sl0 && !sl3 && !sl2)
    return HB_NFC_ERR_PROTOCOL;
  switch (identity->sak) {
    case PLUS_SL3_SAK:
      *out_level = sl0 ? 0 : 3;
      break;
    case PLUS_SL1_2K:
    case PLUS_SL1_4K:
      *out_level = 1;
      break;
    case PLUS_SL2_2K:
    case PLUS_SL2_4K:
      *out_level = 2;
      break;
    default:
      *out_level = PLUS_LEVEL_UNKNOWN;
      break;
  }
  if (!version)
    memset(card->version, 0, sizeof(card->version));
  if (version && card->version[PLUS_VERSION_STORAGE] == PLUS_STORAGE_2K)
    card->block_count = PLUS_MEMORY_2K;
  else if (version && card->version[PLUS_VERSION_STORAGE] == PLUS_STORAGE_4K)
    card->block_count = PLUS_MEMORY_4K;
  else if (type != NULL && !strcmp(type, "Mifare Plus SE"))
    card->block_count = PLUS_MEMORY_1K;
  else if (identity->sak == PLUS_SL1_2K || identity->sak == PLUS_SL2_2K)
    card->block_count = PLUS_MEMORY_2K;
  else if (identity->sak == PLUS_SL1_4K || identity->sak == PLUS_SL2_4K)
    card->block_count = PLUS_MEMORY_4K;
  else if (identity->atqa[0] & PLUS_SMALL_BLOCKS)
    card->block_count = PLUS_MEMORY_2K;
  else if (identity->atqa[0] & 2U)
    card->block_count = PLUS_MEMORY_4K;
  else
    card->block_count = 0;
  if (type == NULL)
    type = "Mifare Plus";
  if (version)
    type = card->version[PLUS_VERSION_MAJOR] == PLUS_EV1   ? "Mifare Plus EV1"
           : card->version[PLUS_VERSION_MAJOR] == PLUS_EV2 ? "Mifare Plus EV2"
                                                           : "Mifare Plus";
  memcpy(card->card_type, type, strlen(type) + 1);
  memcpy(card->identity.uid, identity->uid, identity->uid_len);
  card->identity.uid_len = identity->uid_len;
  memcpy(card->identity.atqa, identity->atqa, sizeof(card->identity.atqa));
  card->identity.sak = identity->sak;
  const uint8_t ats[] = {3U, PLUS_ATS_T0, PLUS_ATS_TC1};
  memcpy(card->identity.ats, ats, sizeof(ats));
  card->identity.ats_len = sizeof(ats);
  return HB_NFC_OK;
}

static bool read_block(mfp_session_t *session,
                       uint16_t key_address,
                       const uint8_t *key,
                       uint16_t address,
                       uint8_t *out) {
  for (unsigned mode = 0; mode < 2U; mode++) {
    if (!session->authenticated && mfp_sl3_auth_first(session, key_address, key) != HB_NFC_OK)
      return false;
    size_t length = 0;
    if (mfp_sl3_read(session, address, 1, mode == 0, out, MFP_BLOCK_SIZE, &length) == HB_NFC_OK)
      return true;
  }
  return false;
}

hb_nfc_err_t nfc_plus_poller_read(const nfc_iso14443a_data_t *identity,
                                  const uint8_t *keys,
                                  size_t key_count,
                                  nfc_plus_poller_cancel_cb_t cancel,
                                  nfc_plus_file_t *out_card,
                                  uint8_t *out_security_level) {
  if (identity == NULL || out_card == NULL || out_security_level == NULL ||
      (key_count && keys == NULL))
    return HB_NFC_ERR_PARAM;
  memset(out_card, 0, sizeof(*out_card));
  mfp_session_t session;
  *out_security_level = PLUS_LEVEL_UNKNOWN;
  hb_nfc_err_t err = identify(identity, &session, out_card, out_security_level);
  if (err != HB_NFC_OK)
    return err;
  if (*out_security_level != 3 || !out_card->block_count)
    return HB_NFC_OK;
  out_card->has_signature = mfp_read_signature(&session, out_card->signature) == HB_NFC_OK;
  unsigned sectors = sector_count(out_card);
  for (unsigned slot = 0; slot < sectors * 2U + PLUS_ADMIN_COUNT; slot++) {
    bool admin = slot >= sectors * 2U;
    unsigned index = admin ? slot - sectors * 2U : slot;
    uint16_t address = admin ? ADMIN_KEYS[index] : PLUS_KEY_BASE + slot;
    for (size_t candidate = 0; candidate < key_count; candidate++) {
      if (cancelled(cancel))
        return ESP_ERR_INVALID_STATE;
      const uint8_t *key = keys + candidate * MFP_BLOCK_SIZE;
      err = mfp_sl3_auth_first(&session, address, key);
      if (err == HB_NFC_ERR_AUTH)
        continue;
      if (err != HB_NFC_OK)
        return err;
      if (admin) {
        memcpy(out_card->extra[index], key, MFP_BLOCK_SIZE);
        out_card->extra_present[index] = out_card->has_extra[index] = true;
      } else {
        memcpy(out_card->keys[index], key, MFP_BLOCK_SIZE);
        out_card->has_key[index] = true;
      }
      unsigned begin = admin ? 0 : first_block(slot / 2U);
      unsigned end = admin ? (index < 2U ? PLUS_ADMIN_COUNT : 0) : first_block(slot / 2U + 1);
      for (unsigned block = begin; block < end; block++) {
        if (cancelled(cancel))
          return ESP_ERR_INVALID_STATE;
        bool *known =
            admin ? &out_card->has_extra[PLUS_ADMIN_COUNT + block] : &out_card->has_block[block];
        uint8_t *data = admin ? out_card->extra[PLUS_ADMIN_COUNT + block] : out_card->blocks[block];
        if (*known)
          continue;
        *known = read_block(&session, address, key, admin ? PLUS_CONFIG_BASE + block : block, data);
        if (admin)
          out_card->extra_present[PLUS_ADMIN_COUNT + block] = true;
      }
      break;
    }
  }
  ESP_LOGI(TAG, "Plus scan finished: %u sectors", sectors);
  return HB_NFC_OK;
}

hb_nfc_err_t nfc_plus_poller_write(const nfc_iso14443a_data_t *identity,
                                   const nfc_plus_file_t *card,
                                   bool is_encrypted,
                                   nfc_plus_poller_cancel_cb_t cancel,
                                   uint16_t *out_written) {
  if (out_written != NULL)
    *out_written = 0;
  if (identity == NULL || card == NULL || out_written == NULL ||
      (card->block_count != PLUS_MEMORY_1K && card->block_count != PLUS_MEMORY_2K &&
       card->block_count != PLUS_MEMORY_4K))
    return HB_NFC_ERR_PARAM;
  if ((identity->uid_len != 4U && identity->uid_len != 7U) ||
      identity->uid_len != card->identity.uid_len || identity->sak != card->identity.sak ||
      memcmp(identity->uid, card->identity.uid, identity->uid_len))
    return HB_NFC_ERR_PROTOCOL;
  mfp_session_t session;
  hb_nfc_err_t err = mfp_poller_init(identity, &session);
  if (err != HB_NFC_OK)
    return err;
  for (unsigned sector = 0; sector < sector_count(card); sector++) {
    if (cancelled(cancel))
      return ESP_ERR_INVALID_STATE;
    bool authenticated = false;
    bool has_candidate = false;
    for (unsigned key_b = 0; key_b < 2U; key_b++) {
      unsigned index = sector * 2U + key_b;
      if (!card->has_key[index])
        continue;
      has_candidate = true;
      err = mfp_sl3_auth_first(&session, PLUS_KEY_BASE + index, card->keys[index]);
      if (err == HB_NFC_OK) {
        authenticated = true;
        break;
      }
      if (err != HB_NFC_ERR_AUTH)
        return err;
    }
    if (!authenticated && has_candidate)
      return HB_NFC_ERR_AUTH;
    if (!authenticated)
      continue;
    unsigned end = first_block(sector + 1) - 1;
    for (unsigned block = first_block(sector); block < end; block++) {
      if (!block || !card->has_block[block])
        continue;
      if (cancelled(cancel))
        return ESP_ERR_INVALID_STATE;
      err = mfp_sl3_write(&session, block, is_encrypted, card->blocks[block], MFP_BLOCK_SIZE);
      if (err != HB_NFC_OK)
        return err;
      (*out_written)++;
    }
  }
  return HB_NFC_OK;
}
