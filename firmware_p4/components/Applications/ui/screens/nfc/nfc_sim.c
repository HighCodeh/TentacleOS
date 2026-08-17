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
#include "nvs.h"

static const char *TAG = "NFC_SIM";
#define NVS_NS  "nfc_sim"
#define NVS_KEY "cards"

typedef struct {
  const char *type;
  uint8_t uid_len;
  uint16_t atqa;
  uint8_t sak;
} nfc_template_t;

static const nfc_template_t POOL[] = {
    {"Mifare Classic 1K", 4, 0x0004, 0x08},
    {"Mifare Classic 4K", 4, 0x0002, 0x18},
    {"NTAG215", 7, 0x0044, 0x00},
    {"Mifare Ultralight", 7, 0x0044, 0x00},
    {"DESFire EV1", 7, 0x0344, 0x20},
};
#define POOL_N ((int)(sizeof(POOL) / sizeof(POOL[0])))

typedef struct {
  int count;
  nfc_sim_card_t cards[NFC_SIM_MAX_SAVED];
} store_t;

static store_t s_store;
static bool s_loaded = false;

static void seed_defaults(void) {
  s_store.count = 0;
  nfc_sim_card_t a = {0};
  strncpy(a.name, "Office Badge", NFC_SIM_NAME_LEN - 1);
  strncpy(a.type, "Mifare Classic 1K", NFC_SIM_TYPE_LEN - 1);
  a.uid_len = 4;
  a.uid[0] = 0x04;
  a.uid[1] = 0xA3;
  a.uid[2] = 0x1C;
  a.uid[3] = 0x9E;
  a.atqa = 0x0004;
  a.sak = 0x08;
  s_store.cards[s_store.count++] = a;

  nfc_sim_card_t b = {0};
  strncpy(b.name, "Metro Pass", NFC_SIM_NAME_LEN - 1);
  strncpy(b.type, "Mifare Ultralight", NFC_SIM_TYPE_LEN - 1);
  b.uid_len = 7;
  b.uid[0] = 0x04;
  b.uid[1] = 0x12;
  b.uid[2] = 0x77;
  b.uid[3] = 0xAB;
  b.uid[4] = 0x33;
  b.uid[5] = 0x10;
  b.uid[6] = 0x80;
  b.atqa = 0x0044;
  b.sak = 0x00;
  s_store.cards[s_store.count++] = b;
}

static void persist(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_blob(h, NVS_KEY, &s_store, sizeof(s_store));
  nvs_commit(h);
  nvs_close(h);
}

void nfc_sim_init(void) {
  if (s_loaded)
    return;
  s_loaded = true;
  nvs_handle_t h;
  size_t len = sizeof(s_store);
  if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    esp_err_t r = nvs_get_blob(h, NVS_KEY, &s_store, &len);
    nvs_close(h);
    if (r == ESP_OK && len == sizeof(s_store) && s_store.count >= 0 &&
        s_store.count <= NFC_SIM_MAX_SAVED) {
      ESP_LOGI(TAG, "loaded %d saved cards", s_store.count);
      return;
    }
  }
  seed_defaults();
  persist();
  ESP_LOGI(TAG, "seeded %d default cards", s_store.count);
}

int nfc_sim_saved_count(void) {
  nfc_sim_init();
  return s_store.count;
}

const nfc_sim_card_t *nfc_sim_saved_get(int index) {
  nfc_sim_init();
  if (index < 0 || index >= s_store.count)
    return NULL;
  return &s_store.cards[index];
}

bool nfc_sim_add(const nfc_sim_card_t *card) {
  nfc_sim_init();
  if (card == NULL || s_store.count >= NFC_SIM_MAX_SAVED)
    return false;
  s_store.cards[s_store.count++] = *card;
  persist();
  return true;
}

void nfc_sim_remove(int index) {
  nfc_sim_init();
  if (index < 0 || index >= s_store.count)
    return;
  for (int i = index; i < s_store.count - 1; i++)
    s_store.cards[i] = s_store.cards[i + 1];
  s_store.count--;
  persist();
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
