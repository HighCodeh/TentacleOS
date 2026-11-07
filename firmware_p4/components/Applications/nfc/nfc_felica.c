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

#include "nfc_felica.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nfc_poller.h"

#define TYPE_STANDARD_LAST  0x48U
#define TYPE_STANDARD_NEW   0xA2U
#define TYPE_LITE_FIRST     0xF0U
#define TYPE_LITE_LAST      0xF2U
#define TRANSCEIVE_TIMEOUT  50U
#define CRC_BYTES           2U
#define BLOCK_ADDRESS_LIMIT 255U
#define READ_DESCRIPTOR     0x80U
#define SYSTEM_ID_MASK      0x0FU
#define SYSTEM_ID_SHIFT     4U
#define PAGE_TEXT_BYTES     8U

static const char *TAG = "NFC_FELICA_CARD";

uint8_t nfc_felica_lite_address(size_t index) {
  if (index <= NFC_FELICA_REG)
    return (uint8_t)index;
  index -= NFC_FELICA_REG + 1;
  if (index <= NFC_FELICA_MC - NFC_FELICA_RC)
    return NFC_FELICA_RC + index;
  index -= NFC_FELICA_MC - NFC_FELICA_RC + 1;
  if (index <= NFC_FELICA_STATE - NFC_FELICA_WCNT)
    return NFC_FELICA_WCNT + index;
  return NFC_FELICA_CRC;
}

int nfc_felica_lite_index(uint16_t address) {
  for (size_t i = 0; i < NFC_FELICA_LITE_BLOCKS; i++)
    if (address == nfc_felica_lite_address(i))
      return (int)i;
  return -1;
}

nfc_felica_type_t nfc_felica_type(const uint8_t *pmm) {
  if (pmm[1] <= TYPE_STANDARD_LAST || pmm[1] == TYPE_STANDARD_NEW)
    return NFC_FELICA_TYPE_STANDARD;
  if (pmm[1] >= TYPE_LITE_FIRST && pmm[1] <= TYPE_LITE_LAST)
    return NFC_FELICA_TYPE_LITE;
  return NFC_FELICA_TYPE_UNKNOWN;
}

void nfc_felica_free(nfc_felica_card_t *card) {
  if (!card)
    return;
  for (size_t i = 0; card->systems && i < card->system_count; i++) {
    free(card->systems[i].services);
    free(card->systems[i].areas);
    free(card->systems[i].blocks);
  }
  free(card->systems);
  memset(card, 0, sizeof(*card));
}

static bool cancelled(const nfc_felica_read_options_t *options) {
  return options && options->should_cancel && options->should_cancel();
}

static esp_err_t exchange(const felica_tag_t *tag,
                          nfc_felica_frame_t *tx,
                          nfc_felica_frame_t *rx,
                          const nfc_felica_read_options_t *options) {
  if (cancelled(options))
    return ESP_ERR_INVALID_STATE;
  tx->data[0] = (uint8_t)tx->size;
  memcpy(tx->data + 2, tag->idm, FELICA_IDM_LEN);
  int received = nfc_poller_transceive(
      tx->data, tx->size, true, rx->data, sizeof(rx->data), 1, TRANSCEIVE_TIMEOUT);
  rx->size = 0;
  if (received <= 0)
    return ESP_ERR_TIMEOUT;
  if (received < (int)NFC_FELICA_HEADER ||
      (received != rx->data[0] && received != rx->data[0] + CRC_BYTES) ||
      rx->data[0] < NFC_FELICA_HEADER || rx->data[1] != tx->data[1] + 1 ||
      memcmp(rx->data + 2, tag->idm, FELICA_IDM_LEN))
    return ESP_ERR_INVALID_RESPONSE;
  rx->size = rx->data[0];
  return ESP_OK;
}

static uint16_t little(const uint8_t *data) {
  return data[0] | ((uint16_t)data[1] << NFC_FELICA_SHIFT);
}

static esp_err_t block_exchange(const felica_tag_t *tag,
                                const uint8_t *addresses,
                                nfc_felica_frame_t *data,
                                const nfc_felica_read_options_t *options) {
  /* size carries the number of requested blocks; returned data retains status bytes. */
  size_t count = data->size;
  nfc_felica_frame_t tx = {.size = 14U + count * 2};
  tx.data[1] = FELICA_CMD_READ_WO_ENC;
  tx.data[10] = 1;
  tx.data[11] = NFC_FELICA_SVC_RO;
  tx.data[13] = count;
  for (size_t i = 0; i < count; i++) {
    tx.data[14 + 2 * i] = READ_DESCRIPTOR;
    tx.data[15 + 2 * i] = addresses[i];
  }
  esp_err_t error = exchange(tag, &tx, data, options);
  if (error != ESP_OK)
    return error;
  if (data->size < NFC_FELICA_STATUS)
    return ESP_ERR_INVALID_RESPONSE;
  if (data->data[10] || data->data[11])
    return data->size == NFC_FELICA_STATUS ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
  return data->size == NFC_FELICA_PAYLOAD + count * NFC_FELICA_BYTES && data->data[12] == count
             ? ESP_OK
             : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t write_auth(const felica_tag_t *tag,
                            nfc_felica_frame_t *tx,
                            const nfc_felica_read_options_t *options) {
  nfc_felica_frame_t rx = {0};
  esp_err_t error = exchange(tag, tx, &rx, options);
  if (error != ESP_OK)
    return error;
  if (rx.size != NFC_FELICA_STATUS)
    return ESP_ERR_INVALID_RESPONSE;
  return rx.data[10] || rx.data[11] ? ESP_ERR_NOT_ALLOWED : ESP_OK;
}

static esp_err_t authenticate(nfc_felica_card_t *card, const nfc_felica_read_options_t *options) {
  nfc_felica_crypto_t crypto = {0};
  uint8_t random[NFC_FELICA_BYTES];
  esp_fill_random(random, sizeof(random));
  esp_err_t error = nfc_felica_crypto_init(&crypto, options->key, random);
  if (error != ESP_OK)
    return error;
  nfc_felica_frame_t tx = {.size = 32U, .data = {0, FELICA_CMD_WRITE_WO_ENC}};
  tx.data[10] = 1;
  tx.data[11] = NFC_FELICA_SVC_RW;
  tx.data[13] = 1;
  tx.data[14] = READ_DESCRIPTOR;
  tx.data[15] = NFC_FELICA_RC;
  memcpy(tx.data + 16, random, sizeof(random));
  error = write_auth(&card->tag, &tx, options);
  if (error != ESP_OK)
    goto cleanup;
  uint8_t addresses[] = {NFC_FELICA_ID, NFC_FELICA_WCNT, NFC_FELICA_MAC_A};
  nfc_felica_frame_t rx = {.size = sizeof(addresses)};
  error = block_exchange(&card->tag, addresses, &rx, options);
  if (error != ESP_OK)
    goto cleanup;
  if (rx.data[10] || rx.data[11]) {
    error = ESP_ERR_NOT_ALLOWED;
    goto cleanup;
  }
  nfc_felica_frame_t authenticated = {.size = 2U * NFC_FELICA_BYTES};
  memcpy(authenticated.data, rx.data + NFC_FELICA_PAYLOAD, authenticated.size);
  uint8_t mac[NFC_FELICA_HALF];
  error = nfc_felica_mac_read(&crypto, addresses, &authenticated, mac);
  if (error != ESP_OK)
    goto cleanup;
  uint8_t difference = 0;
  for (size_t i = 0; i < sizeof(mac); i++)
    difference |= mac[i] ^ rx.data[NFC_FELICA_PAYLOAD + authenticated.size + i];
  if (difference) {
    error = ESP_ERR_NOT_ALLOWED;
    goto cleanup;
  }
  card->is_internal_auth = true;
  memset(&tx, 0, sizeof(tx));
  tx.size = 50U;
  tx.data[1] = FELICA_CMD_WRITE_WO_ENC;
  tx.data[10] = 1;
  tx.data[11] = NFC_FELICA_SVC_RW;
  tx.data[13] = 2;
  tx.data[14] = READ_DESCRIPTOR;
  tx.data[15] = NFC_FELICA_STATE;
  tx.data[16] = READ_DESCRIPTOR;
  tx.data[17] = NFC_FELICA_MAC_A;
  tx.data[18] = 1;
  const uint8_t *counter = authenticated.data + NFC_FELICA_BYTES;
  error = nfc_felica_mac_write(&crypto, counter, tx.data + 18, tx.data + 34);
  if (error != ESP_OK)
    goto cleanup;
  memcpy(tx.data + 42, counter, 3);
  error = write_auth(&card->tag, &tx, options);
  if (error != ESP_OK)
    goto cleanup;
  addresses[0] = NFC_FELICA_STATE;
  rx.size = 1;
  error = block_exchange(&card->tag, addresses, &rx, options);
  if (error != ESP_OK)
    goto cleanup;
  card->is_external_auth = !rx.data[10] && !rx.data[11] && rx.data[NFC_FELICA_PAYLOAD] == 1;
  if (!card->is_external_auth)
    error = ESP_ERR_NOT_ALLOWED;
cleanup:
  memset(&crypto, 0, sizeof(crypto));
  memset(&tx, 0, sizeof(tx));
  return error;
}

static esp_err_t read_lite(nfc_felica_card_t *card, const nfc_felica_read_options_t *options) {
  if (options && options->key) {
    esp_err_t error = authenticate(card, options);
    if (error != ESP_OK && error != ESP_ERR_NOT_ALLOWED)
      return error;
    if (error == ESP_ERR_NOT_ALLOWED)
      ESP_LOGW(TAG, "Lite authentication denied; reading public blocks");
  }
  for (size_t i = 0; i < NFC_FELICA_LITE_BLOCKS; i++) {
    uint8_t address = nfc_felica_lite_address(i);
    nfc_felica_frame_t rx = {.size = 1};
    esp_err_t error = block_exchange(&card->tag, &address, &rx, options);
    if (error != ESP_OK)
      return error;
    nfc_felica_block_t *block = &card->lite[i];
    block->sf1 = rx.data[10];
    block->sf2 = rx.data[11];
    if (!block->sf1 && !block->sf2) {
      memcpy(block->data, rx.data + NFC_FELICA_PAYLOAD, NFC_FELICA_BYTES);
      card->blocks_read++;
    }
    card->blocks_total++;
  }
  if (card->is_internal_auth && card->is_external_auth) {
    card->has_key = true;
    memcpy(card->lite[nfc_felica_lite_index(NFC_FELICA_CK)].data, options->key, NFC_FELICA_BYTES);
  }
  return ESP_OK;
}

static esp_err_t append_item(void **array, size_t *count, size_t size, const void *item) {
  void *resized = realloc(*array, (*count + 1) * size);
  if (!resized) {
    ESP_LOGE(TAG, "Profile allocation failed");
    return ESP_ERR_NO_MEM;
  }
  *array = resized;
  memcpy((uint8_t *)resized + *count * size, item, size);
  (*count)++;
  return ESP_OK;
}

static esp_err_t key_version(const felica_tag_t *tag,
                             uint16_t code,
                             uint16_t *out,
                             const nfc_felica_read_options_t *options) {
  nfc_felica_frame_t tx = {.size = 13U, .data = {0, NFC_FELICA_CMD_SERVICE}}, rx = {0};
  tx.data[10] = 1;
  tx.data[11] = code;
  tx.data[12] = code >> NFC_FELICA_SHIFT;
  esp_err_t error = exchange(tag, &tx, &rx, options);
  if (error != ESP_OK)
    return error;
  if (rx.size != tx.size || rx.data[10] != 1)
    return ESP_ERR_INVALID_RESPONSE;
  *out = little(rx.data + 11);
  return ESP_OK;
}

static esp_err_t read_system(nfc_felica_system_t *system,
                             const felica_tag_t *tag,
                             size_t *total_blocks,
                             const nfc_felica_read_options_t *options) {
  esp_err_t error;
  for (size_t cursor = 0;; cursor++) {
    nfc_felica_frame_t tx = {.size = 12U, .data = {0, NFC_FELICA_CMD_SEARCH}}, rx = {0};
    tx.data[10] = cursor;
    tx.data[11] = cursor >> NFC_FELICA_SHIFT;
    error = exchange(tag, &tx, &rx, options);
    if (error != ESP_OK)
      return error;
    if (rx.size != 12U && rx.size != 14U)
      return ESP_ERR_INVALID_RESPONSE;
    uint16_t code = little(rx.data + 10);
    if (code == NFC_FELICA_UNKNOWN)
      break;
    if (cursor == NFC_FELICA_NODES)
      return ESP_ERR_INVALID_SIZE;
    if (rx.size == 14U) {
      nfc_felica_area_t area = {.code = code,
                                .end = little(rx.data + 12),
                                .first = system->service_count,
                                .key_version = NFC_FELICA_UNKNOWN};
      error = append_item((void **)&system->areas, &system->area_count, sizeof(area), &area);
    } else {
      nfc_felica_service_t service = {.code = code, .key_version = NFC_FELICA_UNKNOWN};
      for (size_t i = 0; i < system->service_count; i++)
        if (system->services[i].code == code)
          return ESP_ERR_INVALID_RESPONSE;
      error = append_item(
          (void **)&system->services, &system->service_count, sizeof(service), &service);
      if (system->area_count)
        system->areas[system->area_count - 1].last = system->service_count - 1;
    }
    if (error != ESP_OK)
      return error;
  }
  error = key_version(tag, NFC_FELICA_UNKNOWN, &system->key_version, options);
  if (error != ESP_OK)
    return error;
  for (size_t i = 0; i < system->area_count; i++) {
    error = key_version(tag, system->areas[i].code, &system->areas[i].key_version, options);
    if (error != ESP_OK)
      return error;
  }
  for (size_t i = 0; i < system->service_count; i++) {
    nfc_felica_service_t *service = &system->services[i];
    error = key_version(tag, service->code, &service->key_version, options);
    if (error != ESP_OK)
      return error;
    if (!(service->code & NFC_FELICA_ATTR_PUBLIC))
      continue;
    for (size_t index = 0; index < BLOCK_ADDRESS_LIMIT; index++) {
      nfc_felica_frame_t tx = {.size = 16U, .data = {0, FELICA_CMD_READ_WO_ENC}}, rx = {0};
      tx.data[10] = 1;
      tx.data[11] = service->code;
      tx.data[12] = service->code >> NFC_FELICA_SHIFT;
      tx.data[13] = 1;
      tx.data[14] = READ_DESCRIPTOR;
      tx.data[15] = index;
      error = exchange(tag, &tx, &rx, options);
      if (error != ESP_OK)
        return error;
      if (rx.size < NFC_FELICA_STATUS)
        return ESP_ERR_INVALID_RESPONSE;
      if (rx.data[10] || rx.data[11]) {
        if (rx.size != NFC_FELICA_STATUS)
          return ESP_ERR_INVALID_RESPONSE;
        break;
      }
      if (rx.size != NFC_FELICA_PAYLOAD + NFC_FELICA_BYTES || rx.data[12] != 1)
        return ESP_ERR_INVALID_RESPONSE;
      if (*total_blocks == NFC_FELICA_BLOCKS)
        return ESP_ERR_INVALID_SIZE;
      nfc_felica_public_t block = {.service = service->code, .index = index};
      memcpy(block.block.data, rx.data + NFC_FELICA_PAYLOAD, NFC_FELICA_BYTES);
      error = append_item((void **)&system->blocks, &system->block_count, sizeof(block), &block);
      if (error != ESP_OK)
        return error;
      (*total_blocks)++;
    }
  }
  return ESP_OK;
}

esp_err_t nfc_felica_read(nfc_felica_card_t *out, const nfc_felica_read_options_t *options) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  nfc_felica_card_t card = {0};
  esp_err_t error = felica_poller_init();
  if (error != ESP_OK)
    return error;
  if (cancelled(options))
    return ESP_ERR_INVALID_STATE;
  error = felica_sensf_req(FELICA_SC_COMMON, &card.tag);
  if (error != ESP_OK)
    return error;
  card.type = nfc_felica_type(card.tag.pmm);
  if (card.type == NFC_FELICA_TYPE_LITE) {
    error = read_lite(&card, options);
    goto cleanup;
  }
  if (card.type == NFC_FELICA_TYPE_UNKNOWN) {
    error = ESP_ERR_NOT_SUPPORTED;
    goto cleanup;
  }
  nfc_felica_frame_t tx = {.size = NFC_FELICA_HEADER, .data = {0, NFC_FELICA_CMD_SYSTEM}}, rx = {0};
  error = exchange(&card.tag, &tx, &rx, options);
  if (error != ESP_OK)
    goto cleanup;
  if (rx.size < 11U || rx.data[10] > NFC_FELICA_SYSTEMS || !rx.data[10] ||
      rx.size != 11U + rx.data[10] * 2U) {
    error = ESP_ERR_INVALID_RESPONSE;
    goto cleanup;
  }
  card.systems = calloc(rx.data[10], sizeof(*card.systems));
  if (!card.systems) {
    ESP_LOGE(TAG, "System allocation failed");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  card.system_count = rx.data[10];
  size_t total_blocks = 0;
  for (size_t i = 0; i < card.system_count; i++) {
    nfc_felica_system_t *system = &card.systems[i];
    system->code = ((uint16_t)rx.data[11 + i * 2] << NFC_FELICA_SHIFT) | rx.data[12 + i * 2];
    system->key_version = NFC_FELICA_UNKNOWN;
    if (cancelled(options)) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    felica_tag_t selected = {0};
    error = felica_sensf_req(system->code, &selected);
    if (error != ESP_OK) {
      ESP_LOGW(TAG, "System %04X unavailable", system->code);
      error = ESP_OK;
      continue;
    }
    if (memcmp(card.tag.idm + 1, selected.idm + 1, FELICA_IDM_LEN - 1) ||
        (card.tag.idm[0] & SYSTEM_ID_MASK) != (selected.idm[0] & SYSTEM_ID_MASK)) {
      error = ESP_ERR_INVALID_RESPONSE;
      goto cleanup;
    }
    error = read_system(system, &selected, &total_blocks, options);
    if (error != ESP_OK)
      goto cleanup;
  }
cleanup:
  if (error == ESP_OK && cancelled(options))
    error = ESP_ERR_INVALID_STATE;
  if (error == ESP_OK)
    *out = card;
  else
    nfc_felica_free(&card);
  return error;
}

size_t nfc_felica_page_count(const nfc_felica_card_t *card) {
  size_t pages = 1 + card->blocks_total;
  for (size_t i = 0; i < card->system_count; i++)
    pages += 1 + card->systems[i].area_count + card->systems[i].service_count +
             card->systems[i].block_count;
  return pages;
}

static void block_page(const nfc_felica_block_t *block, char *out, size_t size) {
  size_t used = strlen(out);
  for (size_t i = 0; i < NFC_FELICA_BYTES && used < size; i++) {
    int written = snprintf(
        out + used, size - used, "%s%02X", i % PAGE_TEXT_BYTES ? " " : "\n", block->data[i]);
    if (written < 0 || (size_t)written >= size - used)
      break;
    used += (size_t)written;
  }
}

void nfc_felica_page(const nfc_felica_card_t *card, size_t page, char *out, size_t size) {
  if (!card || !out || !size)
    return;
  out[0] = 0;
  if (!page) {
    snprintf(out,
             size,
             "FeliCa %s\nIDm %02X%02X%02X%02X%02X%02X%02X%02X\nPMm "
             "%02X%02X%02X%02X%02X%02X%02X%02X\nSystems %u | blocks %u/%u\nLite auth: internal %s "
             "/ external %s",
             card->type == NFC_FELICA_TYPE_LITE ? "Lite" : "Standard",
             card->tag.idm[0],
             card->tag.idm[1],
             card->tag.idm[2],
             card->tag.idm[3],
             card->tag.idm[4],
             card->tag.idm[5],
             card->tag.idm[6],
             card->tag.idm[7],
             card->tag.pmm[0],
             card->tag.pmm[1],
             card->tag.pmm[2],
             card->tag.pmm[3],
             card->tag.pmm[4],
             card->tag.pmm[5],
             card->tag.pmm[6],
             card->tag.pmm[7],
             (unsigned)card->system_count,
             card->blocks_read,
             card->blocks_total,
             card->is_internal_auth ? "yes" : "no",
             card->is_external_auth ? "yes" : "no");
    return;
  }
  page--;
  if (page < card->blocks_total) {
    const nfc_felica_block_t *block = &card->lite[page];
    snprintf(out,
             size,
             "Lite block %02X\nStatus %02X/%02X%s",
             nfc_felica_lite_address(page),
             block->sf1,
             block->sf2,
             block->sf1 || block->sf2 ? "\nUnavailable" : "");
    if (!block->sf1 && !block->sf2)
      block_page(block, out, size);
    return;
  }
  page -= card->blocks_total;
  for (size_t i = 0; i < card->system_count; i++) {
    const nfc_felica_system_t *system = &card->systems[i];
    if (!page) {
      snprintf(out,
               size,
               "System %04X\nKey version %04X\nAreas %u / services %u\nPublic blocks %u",
               system->code,
               system->key_version,
               (unsigned)system->area_count,
               (unsigned)system->service_count,
               (unsigned)system->block_count);
      return;
    }
    page--;
    if (page < system->area_count) {
      const nfc_felica_area_t *area = &system->areas[page];
      snprintf(out,
               size,
               "System %04X\nArea %04X - %04X\nServices #%u-#%u\nKey version %04X",
               system->code,
               area->code,
               area->end,
               area->first,
               area->last,
               area->key_version);
      return;
    }
    page -= system->area_count;
    if (page < system->service_count) {
      const nfc_felica_service_t *service = &system->services[page];
      snprintf(out,
               size,
               "System %04X\nService %04X / attr %02X\nKey version %04X\n%s",
               system->code,
               service->code,
               service->code & NFC_FELICA_ATTR_MASK,
               service->key_version,
               service->code & NFC_FELICA_ATTR_PUBLIC ? "Public access"
                                                      : "Authentication required");
      return;
    }
    page -= system->service_count;
    if (page < system->block_count) {
      const nfc_felica_public_t *block = &system->blocks[page];
      snprintf(out,
               size,
               "System %04X\nService %04X block %02X",
               system->code,
               block->service,
               block->index);
      block_page(&block->block, out, size);
      return;
    }
    page -= system->block_count;
  }
}
