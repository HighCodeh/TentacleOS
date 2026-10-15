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

#include "nfc_sim.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nfc_store.h"

static const char *TAG = "NFC_LIBRARY";

typedef struct {
  const char *type;
  uint8_t uid_len;
  uint16_t atqa;
  uint8_t sak;
} nfc_template_t;

static const nfc_template_t POOL[] = {
    {"MIFARE Classic 1K", 4, 0x0400, 0x08},
    {"MIFARE Classic 4K", 4, 0x0200, 0x18},
    {"NTAG215", 7, 0x4400, 0x00},
    {"MIFARE Ultralight", 7, 0x4400, 0x00},
    {"MIFARE DESFire", 7, 0x4403, 0x20},
};
#define POOL_N ((int)(sizeof(POOL) / sizeof(POOL[0])))

static nfc_sim_card_t s_cards[NFC_SIM_MAX_SAVED];
static int s_count = 0;
static bool s_initialized = false;

static const char *protocol_name(hb_nfc_protocol_t protocol) {
  switch (protocol) {
    case HB_PROTO_ISO14443_3A:
      return "NFC-A / ISO14443-3";
    case HB_PROTO_ISO14443_3B:
      return "NFC-B / ISO14443-3";
    case HB_PROTO_ISO14443_4A:
      return "ISO-DEP (NFC-A)";
    case HB_PROTO_ISO14443_4B:
      return "ISO-DEP (NFC-B)";
    case HB_PROTO_FELICA:
      return "FeliCa / NFC-F";
    case HB_PROTO_ISO15693:
      return "ISO15693 / NFC-V";
    case HB_PROTO_MF_CLASSIC:
      return "MIFARE Classic";
    case HB_PROTO_MF_ULTRALIGHT:
      return "Ultralight / NTAG";
    case HB_PROTO_MF_PLUS:
      return "MIFARE Plus";
    case HB_PROTO_MF_DESFIRE:
      return "MIFARE DESFire";
    case HB_PROTO_ST25TB:
      return "ST25TB";
    case HB_PROTO_SLIX:
      return "SLIX / NFC-V";
    default:
      return "NFC tag";
  }
}

static hb_nfc_protocol_t type_protocol(const nfc_sim_card_t *card) {
  if (strstr(card->type, "Classic") != NULL)
    return HB_PROTO_MF_CLASSIC;
  if (strstr(card->type, "Ultralight") != NULL || strstr(card->type, "NTAG") != NULL)
    return HB_PROTO_MF_ULTRALIGHT;
  if (strstr(card->type, "DESFire") != NULL)
    return HB_PROTO_MF_DESFIRE;
  if (strstr(card->type, "FeliCa") != NULL || strstr(card->type, "NFC-F") != NULL)
    return HB_PROTO_FELICA;
  if (strstr(card->type, "15693") != NULL || strstr(card->type, "NFC-V") != NULL)
    return HB_PROTO_ISO15693;
  if (strstr(card->type, "NFC-B") != NULL)
    return HB_PROTO_ISO14443_3B;
  if (card->sak == 0x20)
    return HB_PROTO_ISO14443_4A;
  return HB_PROTO_ISO14443_3A;
}

static void refresh_cache(void) {
  nfc_store_info_t infos[NFC_SIM_MAX_SAVED];
  s_count = nfc_store_list(infos, NFC_SIM_MAX_SAVED);
  memset(s_cards, 0, sizeof(s_cards));
  for (int i = 0; i < s_count; i++) {
    strlcpy(s_cards[i].name, infos[i].name, sizeof(s_cards[i].name));
    strlcpy(s_cards[i].type, protocol_name(infos[i].protocol), sizeof(s_cards[i].type));
    s_cards[i].uid_len =
        infos[i].uid_len > sizeof(s_cards[i].uid) ? sizeof(s_cards[i].uid) : infos[i].uid_len;
    memcpy(s_cards[i].uid, infos[i].uid, s_cards[i].uid_len);
    s_cards[i].atqa = ((uint16_t)infos[i].atqa[0] << 8) | infos[i].atqa[1];
    s_cards[i].sak = infos[i].sak;
  }
}

void nfc_sim_init(void) {
  if (s_initialized)
    return;
  nfc_store_init();
  s_initialized = true;
  refresh_cache();
  ESP_LOGI(TAG, "real NFC vault loaded: %d entries", s_count);
}

int nfc_sim_saved_count(void) {
  nfc_sim_init();
  refresh_cache();
  return s_count;
}

const nfc_sim_card_t *nfc_sim_saved_get(int index) {
  nfc_sim_init();
  if (index < 0 || index >= s_count)
    return NULL;
  return &s_cards[index];
}

bool nfc_sim_add(const nfc_sim_card_t *card) {
  nfc_sim_init();
  if (card == NULL || card->name[0] == '\0' || card->uid_len == 0 ||
      card->uid_len > sizeof(card->uid))
    return false;
  const uint8_t atqa[2] = {(uint8_t)(card->atqa >> 8), (uint8_t)card->atqa};
  const nfc_store_card_t stored = {
      .name = card->name,
      .protocol = type_protocol(card),
      .uid = card->uid,
      .uid_len = card->uid_len,
      .atqa = {atqa[0], atqa[1]},
      .sak = card->sak,
      .payload = NULL,
      .payload_len = 0,
  };
  const bool ok = nfc_store_save(&stored) == HB_NFC_OK;
  refresh_cache();
  return ok;
}

void nfc_sim_remove(int index) {
  nfc_sim_init();
  if (index < 0 || index >= s_count)
    return;
  (void)nfc_store_delete(index);
  refresh_cache();
}

static void fill_card(int tmpl, const char *prefix, nfc_sim_card_t *out) {
  if (tmpl < 0 || tmpl >= POOL_N)
    tmpl = 0;
  const nfc_template_t *t = &POOL[tmpl];
  memset(out, 0, sizeof(*out));
  out->uid_len = t->uid_len;
  out->atqa = t->atqa;
  out->sak = t->sak;
  strncpy(out->type, t->type, NFC_SIM_TYPE_LEN - 1);
  for (int i = 0; i < out->uid_len; i++)
    out->uid[i] = (uint8_t)(esp_random() & 0xFF);
  if (out->uid_len == 7)
    out->uid[0] = 0x04;
  if (prefix != NULL)
    snprintf(out->name,
             NFC_SIM_NAME_LEN,
             "%s %02X%02X",
             prefix,
             out->uid[out->uid_len - 2],
             out->uid[out->uid_len - 1]);
}

void nfc_sim_random_card(nfc_sim_card_t *out) {
  if (!out)
    return;

  fill_card((int)(esp_random() % POOL_N), NULL, out);
}

int nfc_sim_template_count(void) {
  return POOL_N;
}

void nfc_sim_make_card(int tmpl, nfc_sim_card_t *out) {
  if (out)
    fill_card(tmpl, "Custom", out);
}

void nfc_sim_format_uid(const nfc_sim_card_t *card, char *buf, int buflen) {
  if (buf == NULL || buflen <= 0)
    return;
  buf[0] = '\0';
  if (card == NULL)
    return;
  int off = 0;
  for (int i = 0; i < card->uid_len; i++) {
    int rem = buflen - off;
    if (rem <= 3)
      break;
    off += snprintf(buf + off, (size_t)rem, i ? ":%02X" : "%02X", card->uid[i]);
  }
}
