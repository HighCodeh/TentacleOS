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

#include <string.h>
#include "esp_log.h"

#define SYSTEM_MASK         0x0FU
#define SYSTEM_SHIFT        4U
#define SLOT_MASK           0x0FU
#define POLLING_SIZE        6U
#define POLLING_RESPONSE    18U
#define POLLING_WITH_DATA   20U
#define REQUEST_PERFORMANCE 2U
#define PERFORMANCE_VALUE   0x0083U
#define MAX_SERVICES        16U
#define MAX_STANDARD_BLOCKS 15U
#define MAX_REQUEST_NODES   32U
#define DESCRIPTOR_SHORT    0x80U
#define DESCRIPTOR_MODE     0x70U
#define DESCRIPTOR_INDEX    0x0FU
#define MC_RW               0U
#define MC_SYSTEM           2U
#define MC_SYSTEM_OP        3U
#define MC_KEY_MAC          5U
#define MC_READ_AUTH        6U
#define MC_WRITE_AUTH       8U
#define MC_WRITE_MAC        10U
#define MC_STATE_MAC        12U
#define MC_RESERVED         13U
#define MC_LOCK_BIT         15U
#define COUNTER_BYTES       3U
#define COUNTER_MAX         0xFFFFFFU
#define COUNTER_LOCKED_MAX  0xFFFE00U
#define COUNTER_WARN_START  0x1027U
#define COUNTER_WARN_END    0xFFFDFFU
#define REG_WORD_BYTES      4U
#define REG_DATA_OFFSET     8U

typedef struct {
  uint8_t count;
  uint16_t addresses[MAX_STANDARD_BLOCKS], services[MAX_STANDARD_BLOCKS];
  const uint8_t *payload;
} nfc_felica_request_t;
static const char *TAG = "NFC_FELICA_LISTENER";

static uint16_t little(const uint8_t *p) {
  return p[0] | ((uint16_t)p[1] << NFC_FELICA_SHIFT);
}
static void put16(uint8_t *p, uint16_t n) {
  p[0] = n;
  p[1] = n >> NFC_FELICA_SHIFT;
}
static uint32_t word(const uint8_t *p, size_t size) {
  uint32_t n = 0;
  for (size_t i = 0; i < size; i++)
    n |= (uint32_t)p[i] << (i * NFC_FELICA_SHIFT);
  return n;
}
static nfc_felica_block_t *lite(nfc_felica_card_t *card, uint16_t address) {
  int index = nfc_felica_lite_index(address);
  return index >= 0 && (size_t)index < card->blocks_total ? &card->lite[index] : NULL;
}
static bool available(const nfc_felica_block_t *block) {
  return block && !block->sf1 && !block->sf2;
}
static void identity(nfc_felica_listener_t *listener, uint8_t *out) {
  memcpy(out, listener->card->tag.idm, FELICA_IDM_LEN);
  if (listener->card->type == NFC_FELICA_TYPE_STANDARD)
    out[0] = (out[0] & SYSTEM_MASK) | (listener->system << SYSTEM_SHIFT);
}
static nfc_felica_system_t *system_data(nfc_felica_listener_t *listener) {
  return listener->system < listener->card->system_count
             ? &listener->card->systems[listener->system]
             : NULL;
}
static void status(nfc_felica_frame_t *out, uint8_t first, uint8_t second) {
  out->size = NFC_FELICA_STATUS;
  out->data[10] = first;
  out->data[11] = second;
}

void nfc_felica_listener_init(nfc_felica_listener_t *listener, nfc_felica_card_t *card) {
  memset(listener, 0, sizeof(*listener));
  listener->card = card;
  nfc_felica_block_t *mc = lite(card, NFC_FELICA_MC);
  if (mc)
    memcpy(listener->mc_shadow, mc->data, NFC_FELICA_BYTES);
  nfc_felica_listener_reset(listener);
}
void nfc_felica_listener_reset(nfc_felica_listener_t *listener) {
  listener->system = 0;
  listener->has_rc = false;
  listener->is_authenticated = false;
  memset(&listener->crypto, 0, sizeof(listener->crypto));
  if (listener->card->type != NFC_FELICA_TYPE_LITE)
    return;
  nfc_felica_block_t *mc = lite(listener->card, NFC_FELICA_MC),
                     *state = lite(listener->card, NFC_FELICA_STATE);
  if (mc)
    memcpy(mc->data, listener->mc_shadow, NFC_FELICA_BYTES);
  if (state)
    state->data[0] = 0;
  nfc_felica_block_t *counter = lite(listener->card, NFC_FELICA_WCNT);
  if (mc && counter && !mc->data[MC_SYSTEM] &&
      word(counter->data, COUNTER_BYTES) > COUNTER_LOCKED_MAX)
    memset(counter->data, 0, COUNTER_BYTES);
}

static bool matches(uint16_t requested, uint16_t code) {
  return requested == NFC_FELICA_UNKNOWN || requested == code || requested == (code | 0x00FFU) ||
         requested == (code | 0xFF00U);
}
static esp_err_t polling(nfc_felica_listener_t *listener,
                         const uint8_t *data,
                         size_t size,
                         nfc_felica_frame_t *out) {
  if (size != POLLING_SIZE || data[4] > REQUEST_PERFORMANCE ||
      (data[5] != 0 && data[5] != 1 && data[5] != 3 && data[5] != 7 && data[5] != SLOT_MASK))
    return ESP_ERR_INVALID_RESPONSE;
  uint16_t requested = ((uint16_t)data[2] << NFC_FELICA_SHIFT) | data[3],
           selected = NFC_FELICA_UNKNOWN;
  listener->system = 0;
  if (listener->card->type == NFC_FELICA_TYPE_LITE) {
    nfc_felica_block_t *mc = lite(listener->card, NFC_FELICA_MC);
    if (available(mc) && mc->data[MC_SYSTEM_OP] == 1 && matches(requested, FELICA_SC_NDEF))
      selected = FELICA_SC_NDEF;
    else if (matches(requested, NFC_FELICA_LITE_SYSTEM))
      selected = NFC_FELICA_LITE_SYSTEM;
  } else {
    for (size_t i = 0; i < listener->card->system_count; i++)
      if (matches(requested, listener->card->systems[i].code)) {
        listener->system = i;
        selected = listener->card->systems[i].code;
        break;
      }
  }
  if (selected == NFC_FELICA_UNKNOWN)
    return ESP_OK;
  out->size = data[4] ? POLLING_WITH_DATA : POLLING_RESPONSE;
  out->data[1] = FELICA_CMD_SENSF_RES;
  identity(listener, out->data + 2);
  memcpy(out->data + 10, listener->card->tag.pmm, FELICA_PMM_LEN);
  memset(out->data + 12, UINT8_MAX, FELICA_PMM_LEN - 2);
  uint16_t optional = data[4] == REQUEST_PERFORMANCE ? PERFORMANCE_VALUE : selected;
  out->data[18] = optional >> NFC_FELICA_SHIFT;
  out->data[19] = optional;
  return ESP_OK;
}

static esp_err_t parse_blocks(const uint8_t *data,
                              size_t size,
                              nfc_felica_request_t *request,
                              nfc_felica_frame_t *out) {
  if (size <= NFC_FELICA_HEADER)
    return ESP_ERR_INVALID_RESPONSE;
  size_t services = data[10], pos = 11U + services * 2U;
  if (!services || services > MAX_SERVICES) {
    status(out, UINT8_MAX, NFC_FELICA_SF_SERVICE_COUNT);
    return ESP_OK;
  }
  if (pos >= size)
    return ESP_ERR_INVALID_RESPONSE;
  request->count = data[pos++];
  if (!request->count || request->count > MAX_STANDARD_BLOCKS) {
    status(out, UINT8_MAX, NFC_FELICA_SF_BLOCK_COUNT);
    return ESP_OK;
  }
  for (size_t i = 0; i < request->count; i++) {
    if (pos >= size)
      return ESP_ERR_INVALID_RESPONSE;
    uint8_t descriptor = data[pos++];
    size_t width = descriptor & DESCRIPTOR_SHORT ? 1 : 2;
    if (width > size - pos)
      return ESP_ERR_INVALID_RESPONSE;
    request->addresses[i] = data[pos];
    if (width == 2)
      request->addresses[i] |= (uint16_t)data[pos + 1] << NFC_FELICA_SHIFT;
    pos += width;
    if ((descriptor & DESCRIPTOR_INDEX) >= services) {
      status(out, UINT8_MAX, NFC_FELICA_SF_ORDER);
      return ESP_OK;
    }
    if (descriptor & DESCRIPTOR_MODE) {
      status(out, UINT8_MAX, NFC_FELICA_SF_MODE);
      return ESP_OK;
    }
    request->services[i] = little(data + 11 + (descriptor & DESCRIPTOR_INDEX) * 2);
  }
  size_t bytes = data[1] == FELICA_CMD_WRITE_WO_ENC ? request->count * NFC_FELICA_BYTES : 0;
  if (size - pos != bytes)
    return ESP_ERR_INVALID_RESPONSE;
  request->payload = data + pos;
  return ESP_OK;
}

static esp_err_t standard_blocks(nfc_felica_listener_t *listener,
                                 const uint8_t *data,
                                 const nfc_felica_request_t *request,
                                 nfc_felica_frame_t *out) {
  nfc_felica_system_t *system = system_data(listener);
  nfc_felica_public_t *blocks[MAX_STANDARD_BLOCKS] = {0};
  bool is_write = data[1] == FELICA_CMD_WRITE_WO_ENC;
  for (size_t i = 0; i < request->count; i++) {
    nfc_felica_service_t *service = NULL;
    if (system)
      for (size_t j = 0; j < system->service_count; j++)
        if (system->services[j].code == request->services[i])
          service = &system->services[j];
    if (!service || (is_write && (service->code & NFC_FELICA_ATTR_RO))) {
      status(out, UINT8_MAX, NFC_FELICA_SF_SERVICE);
      return ESP_OK;
    }
    for (size_t j = 0; j < system->block_count; j++)
      if (system->blocks[j].service == service->code &&
          system->blocks[j].index == request->addresses[i])
        blocks[i] = &system->blocks[j];
    if (!blocks[i] || !available(&blocks[i]->block)) {
      status(out, UINT8_MAX, NFC_FELICA_SF_BLOCK);
      return ESP_OK;
    }
  }
  for (size_t i = 0; i < request->count; i++) {
    if (is_write)
      memcpy(blocks[i]->block.data, request->payload + i * NFC_FELICA_BYTES, NFC_FELICA_BYTES);
    else
      memcpy(out->data + NFC_FELICA_PAYLOAD + i * NFC_FELICA_BYTES,
             blocks[i]->block.data,
             NFC_FELICA_BYTES);
  }
  if (is_write) {
    listener->is_dirty = true;
    status(out, 0, 0);
  } else {
    out->size = NFC_FELICA_PAYLOAD + request->count * NFC_FELICA_BYTES;
    out->data[12] = request->count;
  }
  return ESP_OK;
}
static bool mc_bit(const uint8_t *mc, size_t offset, uint16_t block) {
  return block <= MC_LOCK_BIT && (little(mc + offset) & (1U << block));
}
static bool readonly(const uint8_t *mc, uint16_t address) {
  if (address <= NFC_FELICA_REG)
    return !mc_bit(mc, MC_RW, address);
  if (address == NFC_FELICA_ID || address == NFC_FELICA_SER || address == NFC_FELICA_CKV ||
      address == NFC_FELICA_CK)
    return mc[MC_SYSTEM] == 0;
  return address == NFC_FELICA_MAC || address == NFC_FELICA_WCNT || address == NFC_FELICA_DID ||
         address == NFC_FELICA_CRC;
}
static bool needs_mac(const uint8_t *mc, uint16_t address) {
  if (address <= NFC_FELICA_REG)
    return mc_bit(mc, MC_WRITE_MAC, address);
  if (address == NFC_FELICA_CK || address == NFC_FELICA_CKV)
    return mc[MC_KEY_MAC] & 1U;
  return address == NFC_FELICA_STATE && (mc[MC_STATE_MAC] & 1U);
}
static void write_lite(nfc_felica_listener_t *listener, uint16_t address, const uint8_t *data) {
  nfc_felica_card_t *card = listener->card;
  nfc_felica_block_t *block = lite(card, address), *mc = lite(card, NFC_FELICA_MC);
  if (address == NFC_FELICA_REG) {
    bool is_lower = true;
    for (size_t i = 0; i < REG_DATA_OFFSET; i += REG_WORD_BYTES) {
      if (word(data + i, REG_WORD_BYTES) <= word(block->data + i, REG_WORD_BYTES))
        memcpy(block->data + i, data + i, REG_WORD_BYTES);
      else
        is_lower = false;
    }
    if (is_lower)
      memcpy(block->data + REG_DATA_OFFSET, data + REG_DATA_OFFSET, NFC_FELICA_HALF);
  } else if (address == NFC_FELICA_MC) {
    bool can_change = mc_bit(mc->data, MC_RW, MC_LOCK_BIT);
    for (size_t i = 0; i < NFC_FELICA_BYTES; i++) {
      if (i < MC_SYSTEM && can_change)
        listener->mc_shadow[i] &= data[i];
      else if (i >= MC_SYSTEM && i <= MC_KEY_MAC && mc->data[MC_SYSTEM] == UINT8_MAX)
        listener->mc_shadow[i] = data[i];
      else if (i >= MC_READ_AUTH && i <= MC_STATE_MAC && can_change)
        listener->mc_shadow[i] |= data[i];
      else if (i >= MC_RESERVED)
        listener->mc_shadow[i] = 0;
    }
  } else if (address == NFC_FELICA_ID)
    memcpy(block->data + NFC_FELICA_HALF, data + NFC_FELICA_HALF, NFC_FELICA_HALF);
  else
    memcpy(block->data, data, NFC_FELICA_BYTES);
  block->sf1 = 0;
  block->sf2 = 0;
  if (address == NFC_FELICA_RC) {
    listener->is_authenticated = false;
    listener->has_rc =
        card->has_key &&
        nfc_felica_crypto_init(&listener->crypto, lite(card, NFC_FELICA_CK)->data, data) == ESP_OK;
  } else if (address == NFC_FELICA_STATE)
    listener->is_authenticated = data[0] == 1;
  else if (address == NFC_FELICA_CK) {
    card->has_key = true;
    listener->has_rc = false;
    listener->is_authenticated = false;
  }
}

static esp_err_t lite_blocks(nfc_felica_listener_t *listener,
                             const uint8_t *data,
                             const nfc_felica_request_t *request,
                             nfc_felica_frame_t *out) {
  bool is_write = data[1] == FELICA_CMD_WRITE_WO_ENC;
  if (data[10] != 1) {
    status(out, UINT8_MAX, NFC_FELICA_SF_SERVICE_COUNT);
    return ESP_OK;
  }
  if (request->count > (is_write ? NFC_FELICA_MAX_WRITE : NFC_FELICA_MAX_READ)) {
    status(out, UINT8_MAX, NFC_FELICA_SF_BLOCK_COUNT);
    return ESP_OK;
  }
  uint16_t service = request->services[0];
  if ((is_write && service != NFC_FELICA_SVC_RW) ||
      (!is_write && service != NFC_FELICA_SVC_RO && service != NFC_FELICA_SVC_RW)) {
    status(out, 1, NFC_FELICA_SF_SERVICE);
    return ESP_OK;
  }
  nfc_felica_card_t *card = listener->card;
  nfc_felica_block_t *mc = lite(card, NFC_FELICA_MC), *counter = lite(card, NFC_FELICA_WCNT);
  if (!available(mc)) {
    status(out, 1, NFC_FELICA_SF_AUTH);
    return ESP_OK;
  }
  bool with_mac = is_write && request->count == 2 && request->addresses[1] == NFC_FELICA_MAC_A;
  bool has_mac = false, has_mac_a = false;
  for (size_t i = 0; i < request->count; i++) {
    uint16_t address = request->addresses[i];
    nfc_felica_block_t *block = lite(card, address);
    uint8_t failure = 0;
    bool is_virtual =
        address == NFC_FELICA_RC || address == NFC_FELICA_CK || address == NFC_FELICA_MAC_A;
    if (!block || (!available(block) && !is_virtual))
      failure = NFC_FELICA_SF_BLOCK;
    else if (is_write && (readonly(mc->data, address) || (i == 1 && address != NFC_FELICA_MAC_A) ||
                          (i == 0 && address == NFC_FELICA_MAC_A) ||
                          (needs_mac(mc->data, address) && !with_mac)))
      failure = NFC_FELICA_SF_BLOCK;
    else if (!is_write && readonly(mc->data, address) && service != NFC_FELICA_SVC_RO)
      failure = NFC_FELICA_SF_BLOCK;
    else if (address <= NFC_FELICA_REG &&
             mc_bit(mc->data, is_write ? MC_WRITE_AUTH : MC_READ_AUTH, address) &&
             !listener->is_authenticated)
      failure = NFC_FELICA_SF_AUTH;
    else if (address == NFC_FELICA_MAC_A && !listener->has_rc)
      failure = NFC_FELICA_SF_MAC;
    if (address == NFC_FELICA_MAC)
      has_mac = true;
    if (address == NFC_FELICA_MAC_A)
      has_mac_a = true;
    if (has_mac && has_mac_a)
      failure = NFC_FELICA_SF_MIXED;
    if (failure) {
      status(out, 1U << i, failure);
      return ESP_OK;
    }
  }
  if (is_write) {
    if (!available(counter) || (mc->data[MC_SYSTEM] != 0 && mc->data[MC_SYSTEM] != UINT8_MAX)) {
      status(out, 1, NFC_FELICA_SF_MAC);
      return ESP_OK;
    }
    uint32_t count = word(counter->data, COUNTER_BYTES),
             maximum = mc->data[MC_SYSTEM] ? COUNTER_MAX : COUNTER_LOCKED_MAX;
    if (with_mac) {
      const uint8_t *incoming = request->payload + NFC_FELICA_BYTES;
      uint8_t mac[NFC_FELICA_HALF];
      esp_err_t error = nfc_felica_mac_write(
          &listener->crypto, incoming + NFC_FELICA_HALF, request->payload, mac);
      if (error != ESP_OK)
        return error;
      uint8_t difference = 0;
      for (size_t i = 0; i < NFC_FELICA_HALF; i++)
        difference |= mac[i] ^ incoming[i];
      /* Reject counter replay even when a valid MAC is reused. */
      if (difference || word(incoming + NFC_FELICA_HALF, COUNTER_BYTES) != count ||
          count >= maximum) {
        status(out, 2, NFC_FELICA_SF_MAC);
        return ESP_OK;
      }
    }
    for (size_t i = 0; i < request->count; i++)
      write_lite(listener, request->addresses[i], request->payload + i * NFC_FELICA_BYTES);
    listener->is_dirty = true;
    if (count < maximum) {
      count++;
      for (size_t i = 0; i < COUNTER_BYTES; i++)
        counter->data[i] = count >> (i * NFC_FELICA_SHIFT);
    }
    if (!mc->data[MC_SYSTEM] && count > COUNTER_WARN_START && count < COUNTER_WARN_END)
      status(out, UINT8_MAX, NFC_FELICA_SF_WARNING);
    else
      status(out, 0, 0);
  } else {
    out->size = NFC_FELICA_PAYLOAD + request->count * NFC_FELICA_BYTES;
    out->data[12] = request->count;
    size_t mac_start = 0;
    for (size_t i = 0; i < request->count; i++) {
      uint16_t address = request->addresses[i];
      uint8_t *destination = out->data + NFC_FELICA_PAYLOAD + i * NFC_FELICA_BYTES;
      if (address == NFC_FELICA_RC || address == NFC_FELICA_CK)
        memset(destination, 0, NFC_FELICA_BYTES);
      else if (address == NFC_FELICA_MAC_A) {
        memset(destination, 0, NFC_FELICA_BYTES);
        if (i == request->count - 1 && i > mac_start) {
          uint8_t blocks[NFC_FELICA_MAX_READ];
          for (size_t j = mac_start; j <= i; j++)
            blocks[j - mac_start] = request->addresses[j];
          nfc_felica_frame_t frame = {.size = (i - mac_start) * NFC_FELICA_BYTES};
          memcpy(frame.data,
                 out->data + NFC_FELICA_PAYLOAD + mac_start * NFC_FELICA_BYTES,
                 frame.size);
          esp_err_t error = nfc_felica_mac_read(&listener->crypto, blocks, &frame, destination);
          if (error != ESP_OK)
            return error;
        } else
          mac_start = i + 1;
      } else
        memcpy(destination, lite(card, address)->data, NFC_FELICA_BYTES);
    }
  }
  return ESP_OK;
}

static uint16_t node_key(nfc_felica_system_t *system, uint16_t code) {
  if (!system)
    return NFC_FELICA_UNKNOWN;
  if (code == NFC_FELICA_UNKNOWN)
    return system->key_version;
  for (size_t i = 0; i < system->area_count; i++)
    if (system->areas[i].code == code)
      return system->areas[i].key_version;
  for (size_t i = 0; i < system->service_count; i++)
    if (system->services[i].code == code)
      return system->services[i].key_version;
  return NFC_FELICA_UNKNOWN;
}
static esp_err_t command(nfc_felica_listener_t *listener,
                         const uint8_t *data,
                         size_t size,
                         nfc_felica_frame_t *out) {
  nfc_felica_system_t *system = system_data(listener);
  switch (data[1]) {
    case FELICA_CMD_READ_WO_ENC:
    case FELICA_CMD_WRITE_WO_ENC: {
      nfc_felica_request_t request = {0};
      esp_err_t error = parse_blocks(data, size, &request, out);
      if (error != ESP_OK || out->size)
        return error;
      return listener->card->type == NFC_FELICA_TYPE_LITE
                 ? lite_blocks(listener, data, &request, out)
                 : standard_blocks(listener, data, &request, out);
    }
    case NFC_FELICA_CMD_RESPONSE:
      if (size != NFC_FELICA_HEADER)
        return ESP_ERR_INVALID_RESPONSE;
      out->size = NFC_FELICA_HEADER + 1;
      out->data[10] = 0;
      return ESP_OK;
    case NFC_FELICA_CMD_SERVICE: {
      if (size < 11U || !data[10] || data[10] > MAX_REQUEST_NODES || size != 11U + data[10] * 2U)
        return ESP_ERR_INVALID_RESPONSE;
      out->size = size;
      out->data[10] = data[10];
      for (size_t i = 0; i < data[10]; i++) {
        uint16_t code = little(data + 11 + i * 2), version = node_key(system, code);
        if (listener->card->type == NFC_FELICA_TYPE_LITE &&
            (code == NFC_FELICA_SVC_RO || code == NFC_FELICA_SVC_RW))
          version = 0;
        put16(out->data + 11 + i * 2, version);
      }
      return ESP_OK;
    }
    case NFC_FELICA_CMD_SYSTEM:
      if (size != NFC_FELICA_HEADER)
        return ESP_ERR_INVALID_RESPONSE;
      out->data[10] =
          listener->card->type == NFC_FELICA_TYPE_LITE ? 1 : listener->card->system_count;
      out->size = 11U + out->data[10] * 2U;
      for (size_t i = 0; i < out->data[10]; i++) {
        uint16_t code = listener->card->type == NFC_FELICA_TYPE_LITE
                            ? NFC_FELICA_LITE_SYSTEM
                            : listener->card->systems[i].code;
        out->data[11 + i * 2] = code >> NFC_FELICA_SHIFT;
        out->data[12 + i * 2] = code;
      }
      return ESP_OK;
    case NFC_FELICA_CMD_SEARCH: {
      if (size != 12U)
        return ESP_ERR_INVALID_RESPONSE;
      size_t cursor = little(data + 10), ai = 0, si = 0, pos = 0;
      uint16_t code = NFC_FELICA_UNKNOWN, end = 0;
      bool is_area = false;
      if (system)
        while (ai < system->area_count || si < system->service_count) {
          bool pick_area = ai < system->area_count && si >= system->areas[ai].first;
          if (pos++ == cursor) {
            is_area = pick_area;
            if (pick_area) {
              code = system->areas[ai].code;
              end = system->areas[ai].end;
            } else if (si < system->service_count)
              code = system->services[si].code;
            break;
          }
          if (pick_area)
            ai++;
          else if (si < system->service_count)
            si++;
          else
            break;
        }
      out->size = is_area ? 14U : 12U;
      put16(out->data + 10, code);
      put16(out->data + 12, end);
      return ESP_OK;
    }
    default:
      return ESP_OK;
  }
}

esp_err_t nfc_felica_listener_frame(nfc_felica_listener_t *listener,
                                    const uint8_t *data,
                                    size_t size,
                                    nfc_felica_frame_t *out) {
  if (!listener || !listener->card || !data || !out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  if (size < 2 || size > NFC_FELICA_FRAME || data[0] != size)
    return ESP_ERR_INVALID_RESPONSE;
  esp_err_t error;
  if (data[1] == FELICA_CMD_SENSF_REQ)
    error = polling(listener, data, size, out);
  else {
    if (size < NFC_FELICA_HEADER)
      return ESP_ERR_INVALID_RESPONSE;
    if (listener->card->type == NFC_FELICA_TYPE_STANDARD) {
      size_t system = data[2] >> SYSTEM_SHIFT;
      if (system >= listener->card->system_count)
        return ESP_OK;
      listener->system = system;
    }
    uint8_t idm[FELICA_IDM_LEN];
    identity(listener, idm);
    if (memcmp(data + 2, idm, sizeof(idm)))
      return ESP_OK;
    memcpy(out->data + 2, idm, sizeof(idm));
    out->data[1] = data[1] + 1;
    error = command(listener, data, size, out);
  }
  if (error != ESP_OK) {
    out->size = 0;
    ESP_LOGD(TAG, "Invalid frame: %d", error);
  }
  out->data[0] = out->size;
  return error;
}
