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

#include "nfc_t4t.h"
#include <string.h>
#include "esp_log.h"
#define BYTE_SHIFT    8U
#define APDU_HEADER   5U
#define APDU_SELECT   0xA4U
#define APDU_READ     0xB0U
#define APDU_WRITE    0xD6U
#define CC_FILE       0xE103U
#define NDEF_FILE     0xE104U
#define DF_FILE       0xE110U
#define SW_OK         0x9000U
#define SW_SYNTAX     0x6700U
#define SW_MISSING    0x6A82U
#define SW_ACCESS     0x6982U
#define SW_OFFSET     0x6B00U
#define SW_CLASS      0x6E00U
#define SW_COMMAND    0x6D00U
#define SW_PARAMETERS 0x6A86U
#define CHUNK_SIZE    255U
#define NLEN_SIZE     2U
static const char *TAG = "NFC_T4T";
static const uint8_t NDEF_AID[] = {0xD2, 0x76, 0, 0, 0x85, 1, 1};

static uint16_t word(const uint8_t *data) {
  return (uint16_t)((data[0] << BYTE_SHIFT) | data[1]);
}
static void put_word(uint8_t *data, uint16_t value) {
  data[0] = value >> BYTE_SHIFT;
  data[1] = value;
}
static void status(nfc_t4t_response_t *response, uint16_t code) {
  put_word(response->data + response->length, code);
  response->length += NLEN_SIZE;
}
size_t nfc_t4t_length(const nfc_t4t_card_t *card) {
  return card == NULL ? 0 : word(card->file);
}
void nfc_t4t_defaults(nfc_t4t_card_t *card) {
  memset(card, 0, sizeof(*card));
  card->cc =
      (t4t_cc_t){NFC_T4T_CC_SIZE, CHUNK_SIZE, CHUNK_SIZE, NDEF_FILE, NFC_T4T_DEFAULT_SIZE, 0, 0};
  const uint8_t uid[] = {4, 0x48, 0x42, 0x54, 0x34, 0, 1};
  memcpy(card->identity.uid, uid, sizeof(uid));
  card->identity.uid_len = sizeof(uid);
  card->identity.atqa[0] = 0x44;
  card->identity.atqa[1] = 3;
  card->identity.sak = 0x20;
  const uint8_t ats[] = {3, 0x48, 0};
  memcpy(card->identity.ats, ats, sizeof(ats));
  card->identity.ats_len = sizeof(ats);
}
void nfc_t4t_listener_reset(nfc_t4t_listener_t *listener) {
  listener->is_selected = false;
  listener->selected_file = 0;
}
static void cc_bytes(const t4t_cc_t *cc, uint8_t out[NFC_T4T_CC_SIZE]) {
  put_word(out, NFC_T4T_CC_SIZE);
  out[2] = 0x20;
  put_word(out + 3, cc->mle < CHUNK_SIZE ? cc->mle : CHUNK_SIZE);
  put_word(out + 5, cc->mlc < CHUNK_SIZE ? cc->mlc : CHUNK_SIZE);
  out[7] = 4;
  out[8] = 6;
  put_word(out + 9, cc->ndef_fid);
  put_word(out + 11, cc->ndef_max);
  out[13] = cc->read_access;
  out[14] = cc->write_access;
}
esp_err_t nfc_t4t_exchange(nfc_t4t_listener_t *listener,
                           const nfc_t4t_request_t *request,
                           nfc_t4t_response_t *out) {
  if (listener == NULL || listener->card == NULL || request == NULL || request->data == NULL ||
      out == NULL)
    return ESP_ERR_INVALID_ARG;
  out->length = 0;
  nfc_t4t_card_t *card = listener->card;
  if (card->cc.ndef_max < NLEN_SIZE || card->cc.ndef_max > NFC_T4T_FILE_SIZE ||
      nfc_t4t_length(card) > card->cc.ndef_max - NLEN_SIZE)
    return ESP_ERR_INVALID_STATE;
  const uint8_t *apdu = request->data;
  size_t length = request->length;
  uint16_t result = SW_OK;
  if (length < APDU_HEADER)
    result = SW_SYNTAX;
  else if (apdu[0] != 0)
    result = SW_CLASS;
  else if (apdu[1] == APDU_SELECT) {
    size_t lc = apdu[4];
    if (length != APDU_HEADER + lc && length != APDU_HEADER + lc + 1)
      result = SW_SYNTAX;
    else if ((apdu[3] != 0 && apdu[3] != 0x0C) || (apdu[2] != 0 && apdu[2] != 4))
      result = SW_PARAMETERS;
    else if (apdu[2] == 4 && lc == sizeof(NDEF_AID) && !memcmp(apdu + APDU_HEADER, NDEF_AID, lc)) {
      listener->is_selected = true;
      listener->selected_file = 0;
    } else if (apdu[2] == 0 && lc == NLEN_SIZE) {
      uint16_t fid = word(apdu + APDU_HEADER);
      if (fid == DF_FILE) {
        listener->is_selected = true;
        listener->selected_file = 0;
      } else if (!listener->is_selected)
        result = SW_ACCESS;
      else if (fid == CC_FILE || fid == card->cc.ndef_fid)
        listener->selected_file = fid;
      else {
        listener->selected_file = 0;
        result = SW_MISSING;
      }
    } else {
      nfc_t4t_listener_reset(listener);
      result = SW_MISSING;
    }
  } else if (apdu[1] == APDU_READ || apdu[1] == APDU_WRITE) {
    bool is_write = apdu[1] == APDU_WRITE;
    size_t offset = word(apdu + 2), count = apdu[4];
    if (!is_write && !count)
      count = CHUNK_SIZE + 1U;
    if ((!is_write && length != APDU_HEADER) ||
        (is_write && (!count || length != APDU_HEADER + count)))
      result = SW_SYNTAX;
    else if (!listener->is_selected || !listener->selected_file)
      result = SW_ACCESS;
    else if (count > (is_write ? card->cc.mlc : card->cc.mle) || count > CHUNK_SIZE)
      result = SW_SYNTAX;
    else if (listener->selected_file == CC_FILE) {
      if (is_write)
        result = SW_ACCESS;
      else if (offset > NFC_T4T_CC_SIZE || count > NFC_T4T_CC_SIZE - offset)
        result = SW_OFFSET;
      else {
        uint8_t cc[NFC_T4T_CC_SIZE];
        cc_bytes(&card->cc, cc);
        memcpy(out->data, cc + offset, count);
        out->length = count;
      }
    } else if (is_write ? card->cc.write_access : card->cc.read_access)
      result = SW_ACCESS;
    else if (offset > card->cc.ndef_max || count > card->cc.ndef_max - offset)
      result = SW_OFFSET;
    else if (is_write) {
      uint8_t nlen[NLEN_SIZE] = {card->file[0], card->file[1]};
      for (size_t i = 0; i < NLEN_SIZE; i++)
        if (offset <= i && i - offset < count)
          nlen[i] = apdu[APDU_HEADER + i - offset];
      if (word(nlen) > card->cc.ndef_max - NLEN_SIZE)
        result = SW_SYNTAX;
      else {
        memcpy(card->file + offset, apdu + APDU_HEADER, count);
        listener->is_dirty = true;
      }
    } else {
      memcpy(out->data, card->file + offset, count);
      out->length = count;
    }
  } else
    result = SW_COMMAND;
  status(out, result);
  ESP_LOGD(TAG, "APDU status %04X", result);
  return ESP_OK;
}
