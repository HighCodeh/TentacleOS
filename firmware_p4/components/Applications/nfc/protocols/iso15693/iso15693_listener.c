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
#include "iso15693_listener.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"

#define UID_BYTES                 8U
#define CRC_BYTES                 2U
#define CRC_POLYNOMIAL            0x8408U
#define CRC_INITIAL               0xFFFFU
#define BYTE_BITS                 8U
#define FLAG_SUBCARRIER           0x01U
#define FLAG_HIGH_RATE            0x02U
#define FLAG_INVENTORY            0x04U
#define FLAG_EXTENSION            0x08U
#define FLAG_SELECT_AFI           0x10U
#define FLAG_ADDRESS_SLOT         0x20U
#define FLAG_OPTION               0x40U
#define FLAG_RESERVED             0x80U
#define CMD_INVENTORY             0x01U
#define CMD_QUIET                 0x02U
#define CMD_READ                  0x20U
#define CMD_WRITE                 0x21U
#define CMD_LOCK                  0x22U
#define CMD_READ_MULTIPLE         0x23U
#define CMD_WRITE_MULTIPLE        0x24U
#define CMD_SELECT                0x25U
#define CMD_RESET                 0x26U
#define CMD_WRITE_AFI             0x27U
#define CMD_LOCK_AFI              0x28U
#define CMD_WRITE_DSFID           0x29U
#define CMD_LOCK_DSFID            0x2AU
#define CMD_SYSTEM                0x2BU
#define CMD_SECURITY              0x2CU
#define CMD_INVENTORY_READ        0xA0U
#define CMD_FAST_INVENTORY_READ   0xA1U
#define INVENTORY_EXTENDED        0x80U
#define INVENTORY_EAS             0x01U
#define INVENTORY_FULL_UID        0x02U
#define INVENTORY_SKIP_DATA       0x10U
#define INVENTORY_QUIET           0x20U
#define INVENTORY_PERSISTENT      0x40U
#define INVENTORY_OPTIONS_MASK    0x73U
#define FEATURE_INVENTORY_EXT     0x20U
#define FEATURE_INVENTORY_EAS     0x40U
#define CMD_SET_EAS               0xA2U
#define CMD_RESET_EAS             0xA3U
#define CMD_LOCK_EAS              0xA4U
#define CMD_EAS_ALARM             0xA5U
#define CMD_PROTECT_EAS           0xA6U
#define CMD_WRITE_EAS_ID          0xA7U
#define CMD_LOCK_PASSWORD         0xB5U
#define CMD_LOCK_PROTECTION       0xB7U
#define CMD_DESTROY               0xB9U
#define CMD_PASSWORD_64           0xBBU
#define CMD_PERSISTENT_QUIET      0xBCU
#define PASSWORD_DESTROY          3U
#define COUNTER_BLOCK             79U
#define COUNTER_BLOCK_COUNT       80U
#define COUNTER_BLOCK_SIZE        4U
#define COUNTER_RESERVED_BYTE     2U
#define COUNTER_PROTECTION_BYTE   3U
#define PERSISTENT_TIME_US        2000000ULL
#define EAS_ID_BITS               16U
#define CMD_NXP_SYSTEM            0xABU
#define CMD_PROTECT_PAGE          0xB6U
#define PROTECTION_CONDITION_MASK 0x33U
#define PROTECTION_HIGH_SHIFT     4U
#define PASSWORD_READ             0U
#define PASSWORD_WRITE            1U
#define PASSWORD_EAS              4U
#define FEATURE_PROTECTION        0x00000001U
#define FEATURE_SIGNATURE         0x00000100U
#define FEATURE_PRIVACY           0x00001000U
#define FEATURE_COUNTER           0x00000002U
#define FEATURE_EAS               0x0000001CU
#define FEATURE_PERSISTENT        0x00000400U
#define FEATURE_DESTROY           0x00002000U
#define LOCK_DSFID_BIT            0x04U
#define LOCK_EAS_BIT              0x02U
#define LOCK_PROTECTION_BIT       0x08U
#define CMD_RANDOM                0xB2U
#define CMD_PASSWORD              0xB3U
#define CMD_WRITE_PASSWORD        0xB4U
#define CMD_PRIVACY               0xBAU
#define CMD_SIGNATURE             0xBDU
#define CUSTOM_START              0xA0U
#define NXP_VENDOR                0x04U
#define ERROR_FLAG                0x01U
#define ERROR_UNSUPPORTED         0x01U
#define ERROR_FORMAT              0x02U
#define ERROR_BLOCK               0x10U
#define ERROR_LOCKED              0x12U
#define ERROR_UNKNOWN             0x0FU
#define INVENTORY_SLOTS           16U
#define SLOT_BITS                 4U
#define PASSWORD_BYTES            4U
#define PASSWORD_PRIVACY          2U
#define PASSWORD_NONCE_BYTES      2U
#define SYSINFO_FLAGS             0x0FU
#define RESPONSE_FLAGS            0U

static const char *TAG = "ISO15693_LISTENER";

/* SL2S2602 Rev. 4.2, section 9.5.3.15; bytes transmit least-significant bit first. */
static const uint8_t EAS_SEQUENCE[] = {
    0x2F, 0xB3, 0x62, 0x70, 0xD5, 0xA7, 0x90, 0x7F, 0xE8, 0xB1, 0x80, 0x38, 0xD2, 0x81, 0x49, 0x76,
    0x82, 0xDA, 0x9A, 0x86, 0x6F, 0xAF, 0x8B, 0xB0, 0xF1, 0x9C, 0xD1, 0x12, 0xA5, 0x72, 0x37, 0xEF};

static uint16_t crc(const uint8_t *data, size_t length) {
  uint16_t value = CRC_INITIAL;
  for (size_t i = 0; i < length; i++) {
    value ^= data[i];
    for (unsigned bit = 0; bit < BYTE_BITS; bit++)
      value = (uint16_t)((value >> 1) ^ ((value & 1) ? CRC_POLYNOMIAL : 0));
  }
  return (uint16_t)~value;
}

static size_t finish(uint8_t *out, size_t length) {
  uint16_t value = crc(out, length);
  out[length++] = (uint8_t)value;
  out[length++] = (uint8_t)(value >> BYTE_BITS);
  return length;
}

static size_t error_response(uint8_t *out, uint8_t code) {
  out[0] = ERROR_FLAG;
  out[1] = code;
  return finish(out, 2);
}

static size_t inventory(iso15693_listener_t *listener, uint8_t *out) {
  out[0] = RESPONSE_FLAGS;
  out[1] = listener->card->dsfid;
  memcpy(out + 2, listener->card->uid, UID_BYTES);
  return finish(out, 2 + UID_BYTES);
}

static bool mask_matches(const uint8_t *uid, const uint8_t *mask, size_t bits) {
  for (size_t bit = 0; bit < bits; bit++)
    if (((uid[bit / BYTE_BITS] ^ mask[bit / BYTE_BITS]) >> (bit % BYTE_BITS)) & 1)
      return false;
  return true;
}

static unsigned password_index(uint8_t id) {
  for (unsigned i = 0; i < ISO15693_LISTENER_PASSWORDS; i++)
    if (id == (1U << i))
      return i;
  return ISO15693_LISTENER_PASSWORDS;
}

static bool is_counter(const iso15693_listener_card_t *card, size_t block) {
  return card->is_slix && card->block_count == COUNTER_BLOCK_COUNT &&
         card->block_size == COUNTER_BLOCK_SIZE && block == COUNTER_BLOCK;
}

static uint8_t write_counter(iso15693_listener_t *listener, const uint8_t *data) {
  uint8_t *counter = listener->card->memory + COUNTER_BLOCK * COUNTER_BLOCK_SIZE;
  if (data[COUNTER_RESERVED_BYTE] || data[COUNTER_PROTECTION_BYTE] > 1)
    return ERROR_FORMAT;
  if (data[0] == 1 && !data[1] && !data[COUNTER_PROTECTION_BYTE]) {
    if (counter[COUNTER_PROTECTION_BYTE] && !listener->authenticated[PASSWORD_READ])
      return ERROR_LOCKED;
    uint16_t value = (uint16_t)(counter[0] | ((uint16_t)counter[1] << BYTE_BITS));
    value++;
    counter[0] = (uint8_t)value;
    counter[1] = (uint8_t)(value >> BYTE_BITS);
  } else {
    if (!listener->authenticated[PASSWORD_WRITE])
      return ERROR_LOCKED;
    memcpy(counter, data, COUNTER_BLOCK_SIZE);
  }
  return 0;
}

static bool protected_block(const iso15693_listener_t *listener, size_t block, unsigned password) {
  const iso15693_listener_card_t *card = listener->card;
  if (!card->is_slix || !card->has_system_info || is_counter(card, block))
    return false;
  unsigned bit = password + (block >= card->protection_pointer ? PROTECTION_HIGH_SHIFT : 0);
  if (!(card->protection_condition & (1U << bit)))
    return false;
  return !listener->authenticated[password] ||
         (card->is_password_64 &&
          (!listener->authenticated[PASSWORD_READ] || !listener->authenticated[PASSWORD_WRITE]));
}

static size_t inventory_reply(iso15693_listener_t *listener, uint8_t *out) {
  size_t size = listener->inventory_reply_length;
  memcpy(out, listener->inventory_reply, size);
  uint8_t state = listener->inventory_options & (INVENTORY_QUIET | INVENTORY_PERSISTENT);
  if (state == INVENTORY_QUIET)
    listener->is_quiet = true;
  if (state == INVENTORY_PERSISTENT)
    listener->is_persistent_quiet = true;
  return size;
}

static size_t
inventory_read(iso15693_listener_t *listener, const uint8_t *frame, size_t size, uint8_t *out) {
  iso15693_listener_card_t *card = listener->card;
  uint8_t flags = frame[0];
  bool inventory_mode = (flags & FLAG_INVENTORY) != 0;
  bool single = !inventory_mode || (flags & FLAG_ADDRESS_SLOT);
  size_t offset = 3;
  if (!card->is_slix || card->is_private || size <= offset || frame[2] != NXP_VENDOR)
    return 0;
  if (!inventory_mode && (!(flags & (FLAG_ADDRESS_SLOT | FLAG_SELECT_AFI)) ||
                          ((flags & FLAG_ADDRESS_SLOT) && (flags & FLAG_SELECT_AFI)) ||
                          ((flags & FLAG_SELECT_AFI) && !listener->is_selected)))
    return 0;
  if (inventory_mode && (flags & FLAG_SELECT_AFI)) {
    if (offset >= size || (frame[offset] && frame[offset] != card->afi))
      return 0;
    offset++;
  }
  if (offset >= size)
    return 0;
  uint8_t bits = frame[offset++];
  uint8_t options = 0;
  if (bits & INVENTORY_EXTENDED) {
    bits &= ~INVENTORY_EXTENDED;
    if (offset >= size)
      return 0;
    options = frame[offset++];
    if (options & ~INVENTORY_OPTIONS_MASK)
      return 0;
  }
  if (bits > UID_BYTES * BYTE_BITS - (single ? 0 : SLOT_BITS))
    return 0;
  size_t mask_bytes = (bits + BYTE_BITS - 1) / BYTE_BITS;
  if (size - offset < mask_bytes || !mask_matches(card->uid, frame + offset, bits))
    return 0;
  if (!inventory_mode && (flags & FLAG_ADDRESS_SLOT) && bits != UID_BYTES * BYTE_BITS)
    return 0;
  offset += mask_bytes;
  uint8_t state = options & (INVENTORY_QUIET | INVENTORY_PERSISTENT);
  if (inventory_mode &&
      (listener->is_quiet || (listener->is_persistent_quiet && !(flags & FLAG_SELECT_AFI) &&
                              state != (INVENTORY_QUIET | INVENTORY_PERSISTENT))))
    return 0;
  if (state == (INVENTORY_QUIET | INVENTORY_PERSISTENT) && !listener->is_persistent_quiet)
    return 0;
  if ((options & INVENTORY_EAS) && (!card->has_eas || !card->is_eas_enabled))
    return 0;
  size_t first = 0, blocks = 0;
  if (options & INVENTORY_SKIP_DATA) {
    if (offset != size)
      return 0;
  } else {
    if (size - offset != 2)
      return 0;
    first = frame[offset];
    blocks = (size_t)frame[offset + 1] + 1;
    if (first >= card->block_count)
      return 0;
    if (blocks > card->block_count - first)
      blocks = card->block_count - first;
    for (size_t i = first; i < first + blocks; i++)
      if (protected_block(listener, i, PASSWORD_READ))
        return 0;
  }
  uint8_t *reply = listener->inventory_reply;
  size_t length = 1;
  reply[0] = RESPONSE_FLAGS;
  if (flags & FLAG_OPTION) {
    size_t start =
        (options & INVENTORY_FULL_UID) ? 0 : (bits + (single ? 0 : SLOT_BITS)) / BYTE_BITS;
    memcpy(reply + length, card->uid + start, UID_BYTES - start);
    length += UID_BYTES - start;
  }
  size_t bytes = blocks * card->block_size;
  if (length + bytes + CRC_BYTES > sizeof(listener->inventory_reply))
    return 0;
  memcpy(reply + length, card->memory + first * card->block_size, bytes);
  listener->inventory_reply_length = finish(reply, length + bytes);
  listener->inventory_options = options;
  listener->current_slot = listener->inventory_slot = 0;
  if (!single) {
    for (unsigned i = 0; i < SLOT_BITS; i++) {
      size_t bit = bits + i;
      listener->inventory_slot |= ((card->uid[bit / BYTE_BITS] >> (bit % BYTE_BITS)) & 1U) << i;
    }
    listener->has_inventory = true;
  }
  return listener->inventory_slot == 0 ? inventory_reply(listener, out) : 0;
}

static size_t custom(iso15693_listener_t *listener,
                     uint8_t command,
                     const uint8_t *args,
                     size_t length,
                     uint8_t flags,
                     uint8_t *out) {
  iso15693_listener_card_t *card = listener->card;
  if (!card->is_slix)
    return error_response(out, ERROR_UNSUPPORTED);
  out[0] = RESPONSE_FLAGS;
  if (command == CMD_PERSISTENT_QUIET) {
    if (length || !(flags & FLAG_ADDRESS_SLOT) || (flags & FLAG_SELECT_AFI))
      return error_response(out, ERROR_FORMAT);
    listener->is_persistent_quiet = true;
    return 0;
  }
  if (command >= CMD_SET_EAS && command <= CMD_WRITE_EAS_ID) {
    if (!card->has_eas)
      return command == CMD_EAS_ALARM ? 0 : error_response(out, ERROR_UNSUPPORTED);
    if (command == CMD_EAS_ALARM) {
      if (!card->is_eas_enabled)
        return 0;
      if (flags & FLAG_OPTION) {
        if (!length || args[0] > EAS_ID_BITS || args[0] % BYTE_BITS ||
            length != 1U + args[0] / BYTE_BITS)
          return 0;
        if (!args[0]) {
          memcpy(out + 1, card->eas_id, sizeof(card->eas_id));
          return finish(out, 1 + sizeof(card->eas_id));
        }
        if (memcmp(args + 1, card->eas_id, args[0] / BYTE_BITS))
          return 0;
      } else if (length) {
        return 0;
      }
      memcpy(out + 1, EAS_SEQUENCE, sizeof(EAS_SEQUENCE));
      return finish(out, 1 + sizeof(EAS_SEQUENCE));
    }
    if (length != (command == CMD_WRITE_EAS_ID ? sizeof(card->eas_id) : 0U))
      return error_response(out, ERROR_FORMAT);
    if (card->is_read_only || ((card->is_eas_protected || command == CMD_PROTECT_EAS) &&
                               !listener->authenticated[PASSWORD_EAS]))
      return error_response(out, ERROR_LOCKED);
    if (command == CMD_PROTECT_EAS) {
      if (flags & FLAG_OPTION)
        card->is_afi_protected = true;
      else
        card->is_eas_protected = true;
    } else {
      if (card->eas_locked)
        return error_response(out, ERROR_LOCKED);
      if (command == CMD_SET_EAS)
        card->is_eas_enabled = true;
      else if (command == CMD_RESET_EAS)
        card->is_eas_enabled = false;
      else if (command == CMD_LOCK_EAS)
        card->eas_locked = true;
      else
        memcpy(card->eas_id, args, sizeof(card->eas_id));
    }
    size_t reply = finish(out, 1);
    if ((flags & FLAG_OPTION) && command != CMD_PROTECT_EAS) {
      memcpy(listener->pending, out, reply);
      listener->pending_length = reply;
      return 0;
    }
    return reply;
  }
  if (command == CMD_LOCK_PASSWORD) {
    if (length != 1)
      return error_response(out, ERROR_FORMAT);
    unsigned index = password_index(args[0]);
    if (index == ISO15693_LISTENER_PASSWORDS || card->is_read_only ||
        !listener->authenticated[index] || card->password_locked[index])
      return error_response(out, ERROR_LOCKED);
    card->password_locked[index] = true;
    return finish(out, 1);
  }
  if (command == CMD_PASSWORD_64 || command == CMD_LOCK_PROTECTION) {
    if (!card->has_system_info)
      return error_response(out, ERROR_UNSUPPORTED);
    if (length != (command == CMD_LOCK_PROTECTION ? 1U : 0U) ||
        (command == CMD_LOCK_PROTECTION && args[0] != card->protection_pointer))
      return error_response(out, ERROR_FORMAT);
    if (card->is_read_only || !listener->authenticated[PASSWORD_READ] ||
        !listener->authenticated[PASSWORD_WRITE])
      return error_response(out, ERROR_LOCKED);
    if (command == CMD_PASSWORD_64)
      card->is_password_64 = true;
    else
      card->protection_locked = true;
    return finish(out, 1);
  }
  if (command == CMD_NXP_SYSTEM && length == 0 && card->has_system_info) {
    out[1] = card->protection_pointer;
    out[2] = card->protection_condition;
    out[3] = (card->afi_locked ? 1 : 0) | (card->dsfid_locked ? LOCK_DSFID_BIT : 0) |
             (card->eas_locked ? LOCK_EAS_BIT : 0) |
             (card->protection_locked ? LOCK_PROTECTION_BIT : 0);
    uint32_t features =
        FEATURE_PROTECTION | FEATURE_PRIVACY | FEATURE_PERSISTENT | FEATURE_DESTROY |
        FEATURE_INVENTORY_EXT | (card->has_eas ? FEATURE_INVENTORY_EAS : 0) |
        (is_counter(card, COUNTER_BLOCK) ? FEATURE_COUNTER : 0) |
        (card->has_eas ? FEATURE_EAS : 0) | (card->has_signature ? FEATURE_SIGNATURE : 0);
    for (unsigned i = 0; i < sizeof(features); i++)
      out[4 + i] = (uint8_t)(features >> (i * BYTE_BITS));
    return finish(out, 4 + sizeof(features));
  }
  if (command == CMD_PROTECT_PAGE && card->has_system_info) {
    if (length != 2 || (args[0] >= card->block_count || is_counter(card, args[0])) ||
        (args[1] & ~PROTECTION_CONDITION_MASK))
      return error_response(out, ERROR_FORMAT);
    if (card->is_read_only || card->protection_locked || !listener->authenticated[PASSWORD_READ] ||
        !listener->authenticated[PASSWORD_WRITE])
      return error_response(out, ERROR_LOCKED);
    card->protection_pointer = args[0];
    card->protection_condition = args[1];
    return finish(out, 1);
  }
  if (command == CMD_RANDOM && length == 0) {
    esp_fill_random(listener->nonce, sizeof(listener->nonce));
    listener->has_nonce = true;
    memcpy(out + 1, listener->nonce, sizeof(listener->nonce));
    return finish(out, 1 + sizeof(listener->nonce));
  }
  if (command == CMD_SIGNATURE && length == 0 && card->has_signature) {
    memcpy(out + 1, card->signature, sizeof(card->signature));
    return finish(out, 1 + sizeof(card->signature));
  }
  if (command == CMD_PASSWORD || command == CMD_PRIVACY || command == CMD_DESTROY) {
    bool privacy = command == CMD_PRIVACY;
    bool destroy = command == CMD_DESTROY;
    if (destroy && (!(flags & (FLAG_ADDRESS_SLOT | FLAG_SELECT_AFI)) || card->is_read_only))
      return error_response(out, ERROR_LOCKED);
    if (length != PASSWORD_BYTES + ((privacy || destroy) ? 0U : 1U) || !listener->has_nonce)
      return error_response(out, ERROR_FORMAT);
    unsigned index = privacy   ? PASSWORD_PRIVACY
                     : destroy ? PASSWORD_DESTROY
                               : password_index(args[0]);
    const uint8_t *password = args + ((privacy || destroy) ? 0 : 1);
    uint8_t difference = 0;
    listener->has_nonce = false;
    if (index == ISO15693_LISTENER_PASSWORDS || !card->has_password[index]) {
      memset(listener->nonce, 0, sizeof(listener->nonce));
      return 0;
    }
    for (unsigned i = 0; i < PASSWORD_BYTES; i++)
      difference |=
          password[i] ^ listener->nonce[i % PASSWORD_NONCE_BYTES] ^ card->passwords[index][i];
    memset(listener->nonce, 0, sizeof(listener->nonce));
    if (difference) {
      listener->authenticated[index] = false;
      return 0;
    }
    listener->authenticated[index] = true;
    if (destroy)
      card->is_destroyed = true;
    if (index == PASSWORD_PRIVACY)
      card->is_private = privacy;
    return finish(out, 1);
  }
  if (command == CMD_WRITE_PASSWORD) {
    size_t expected = 1 + PASSWORD_BYTES;
    if (length != expected)
      return error_response(out, ERROR_FORMAT);
    unsigned index = password_index(args[0]);
    if (index == ISO15693_LISTENER_PASSWORDS || !listener->authenticated[index] ||
        card->is_read_only || card->password_locked[index])
      return error_response(out, ERROR_LOCKED);
    memcpy(card->passwords[index], args + 1, PASSWORD_BYTES);
    listener->authenticated[index] = false;
    return finish(out, 1);
  }
  return error_response(out, ERROR_UNSUPPORTED);
}

esp_err_t iso15693_listener_init(iso15693_listener_t *listener, iso15693_listener_card_t *card) {
  if (!listener || !card || !card->block_count || card->block_count > ISO15693_LISTENER_BLOCKS ||
      !card->block_size || card->block_size > ISO15693_LISTENER_BLOCK_BYTES) {
    ESP_LOGE(TAG, "Invalid emulation geometry");
    return ESP_ERR_INVALID_ARG;
  }
  memset(listener, 0, sizeof(*listener));
  listener->card = card;
  return ESP_OK;
}

void iso15693_listener_reset(iso15693_listener_t *listener) {
  if (!listener)
    return;
  iso15693_listener_card_t *card = listener->card;
  memset(listener, 0, sizeof(*listener));
  listener->card = card;
}

void iso15693_listener_field(iso15693_listener_t *listener, bool is_present, uint64_t now_us) {
  if (!listener || !listener->card)
    return;
  if (!is_present) {
    if (!listener->field_off_us) {
      bool persistent = listener->is_persistent_quiet;
      iso15693_listener_reset(listener);
      listener->is_persistent_quiet = persistent;
      listener->field_off_us = now_us ? now_us : 1;
    }
  }
  if (listener->field_off_us && now_us >= listener->field_off_us &&
      now_us - listener->field_off_us >= PERSISTENT_TIME_US)
    listener->is_persistent_quiet = false;
  if (is_present)
    listener->field_off_us = 0;
}

esp_err_t iso15693_listener_exchange(iso15693_listener_t *listener,
                                     const uint8_t *frame,
                                     size_t length,
                                     bool is_slot,
                                     uint8_t *out,
                                     size_t capacity,
                                     size_t *out_length) {
  if (!listener || !listener->card || !out || !out_length ||
      capacity < ISO15693_LISTENER_REPLY_BYTES || (!frame && length))
    return ESP_ERR_INVALID_ARG;
  *out_length = 0;
  iso15693_listener_card_t *card = listener->card;
  if (card->is_destroyed)
    return ESP_OK;
  if (is_slot) {
    if (length)
      return ESP_ERR_INVALID_ARG;
    if (listener->pending_length) {
      memcpy(out, listener->pending, listener->pending_length);
      *out_length = listener->pending_length;
      listener->pending_length = 0;
    } else if (listener->has_inventory) {
      listener->current_slot++;
      if (listener->current_slot == listener->inventory_slot)
        *out_length = listener->inventory_reply_length ? inventory_reply(listener, out)
                                                       : inventory(listener, out);
      if (listener->current_slot >= INVENTORY_SLOTS - 1)
        listener->has_inventory = false;
    }
    return ESP_OK;
  }
  if (length < 2 + CRC_BYTES ||
      crc(frame, length - CRC_BYTES) !=
          (uint16_t)(frame[length - 2] | ((uint16_t)frame[length - 1] << BYTE_BITS)))
    return ESP_OK;
  uint8_t flags = frame[0], command = frame[1];
  if (flags & (FLAG_SUBCARRIER | FLAG_EXTENSION | FLAG_RESERVED))
    return ESP_OK;
  listener->has_inventory = false;
  listener->pending_length = 0;
  listener->inventory_reply_length = 0;
  listener->is_fast_rate = command == CMD_FAST_INVENTORY_READ;
  listener->is_high_rate = (flags & FLAG_HIGH_RATE) != 0;
  size_t size = length - CRC_BYTES, offset = 2;
  if (command == CMD_INVENTORY_READ || command == CMD_FAST_INVENTORY_READ) {
    *out_length = inventory_read(listener, frame, size, out);
    return ESP_OK;
  }
  if (flags & FLAG_INVENTORY) {
    if (command != CMD_INVENTORY || card->is_private || listener->is_quiet ||
        (listener->is_persistent_quiet && !(flags & FLAG_SELECT_AFI)))
      return ESP_OK;
    if (flags & FLAG_SELECT_AFI) {
      if (offset >= size || (frame[offset] && frame[offset] != card->afi))
        return ESP_OK;
      offset++;
    }
    if (offset >= size)
      return ESP_OK;
    size_t bits = frame[offset++];
    bool single = (flags & FLAG_ADDRESS_SLOT) != 0;
    if (bits > UID_BYTES * BYTE_BITS - (single ? 0 : SLOT_BITS) ||
        size - offset != (bits + BYTE_BITS - 1) / BYTE_BITS ||
        !mask_matches(card->uid, frame + offset, bits))
      return ESP_OK;
    listener->current_slot = listener->inventory_slot = 0;
    if (!single) {
      for (unsigned i = 0; i < SLOT_BITS; i++) {
        size_t bit = bits + i;
        listener->inventory_slot |= ((card->uid[bit / BYTE_BITS] >> (bit % BYTE_BITS)) & 1) << i;
      }
      listener->has_inventory = true;
    }
    if (listener->inventory_slot == 0)
      *out_length = inventory(listener, out);
    return ESP_OK;
  }
  bool is_custom = command >= CUSTOM_START;
  if (is_custom) {
    if (offset >= size || frame[offset++] != NXP_VENDOR)
      return ESP_OK;
  }
  if ((flags & FLAG_ADDRESS_SLOT) && (flags & FLAG_SELECT_AFI))
    return ESP_OK;
  if (flags & FLAG_ADDRESS_SLOT) {
    if (size - offset < UID_BYTES || memcmp(frame + offset, card->uid, UID_BYTES))
      return ESP_OK;
    offset += UID_BYTES;
  } else if ((flags & FLAG_SELECT_AFI) && !listener->is_selected) {
    return ESP_OK;
  } else if (listener->is_quiet || (listener->is_persistent_quiet && !(flags & FLAG_SELECT_AFI) &&
                                    command != CMD_EAS_ALARM)) {
    return ESP_OK;
  }
  if (card->is_private && command != CMD_RANDOM && command != CMD_PASSWORD)
    return ESP_OK;
  const uint8_t *args = frame + offset;
  size_t count = size - offset;
  out[0] = RESPONSE_FLAGS;
  size_t response = 1;
  bool mutation = false;
  uint8_t error = 0;
  if (is_custom) {
    *out_length = custom(listener, command, args, count, flags, out);
    return ESP_OK;
  }
  switch (command) {
    case CMD_QUIET:
    case CMD_SELECT:
    case CMD_RESET:
      if (count || (!(flags & FLAG_ADDRESS_SLOT) && command != CMD_RESET)) {
        error = ERROR_FORMAT;
        break;
      }
      bool persistent = listener->is_persistent_quiet && command == CMD_QUIET;
      iso15693_listener_reset(listener);
      listener->is_persistent_quiet = persistent;
      listener->is_high_rate = (flags & FLAG_HIGH_RATE) != 0;
      listener->is_quiet = command == CMD_QUIET;
      listener->is_selected = command == CMD_SELECT;
      if (command == CMD_QUIET)
        return ESP_OK;
      break;
    case CMD_SYSTEM:
      if (count) {
        error = ERROR_FORMAT;
        break;
      }
      out[response++] = SYSINFO_FLAGS;
      memcpy(out + response, card->uid, UID_BYTES);
      response += UID_BYTES;
      out[response++] = card->dsfid;
      out[response++] = card->afi;
      out[response++] = (uint8_t)(card->block_count - 1);
      out[response++] = card->block_size - 1;
      out[response++] = card->ic_ref;
      break;
    case CMD_READ:
    case CMD_READ_MULTIPLE:
    case CMD_SECURITY: {
      size_t expected = command == CMD_READ ? 1 : 2;
      if (count != expected) {
        error = ERROR_FORMAT;
        break;
      }
      size_t blocks = command == CMD_READ ? 1 : (size_t)args[1] + 1;
      if ((size_t)args[0] + blocks > card->block_count) {
        error = ERROR_BLOCK;
        break;
      }
      size_t bytes =
          command == CMD_SECURITY ? 1 : card->block_size + ((flags & FLAG_OPTION) ? 1 : 0);
      if (blocks * bytes > capacity - response - CRC_BYTES) {
        error = ERROR_BLOCK;
        break;
      }
      for (size_t i = args[0]; i < args[0] + blocks; i++)
        if (command != CMD_SECURITY && protected_block(listener, i, PASSWORD_READ))
          error = ERROR_UNKNOWN;
      if (error)
        break;
      for (size_t i = args[0]; i < args[0] + blocks; i++) {
        if (command == CMD_SECURITY || (flags & FLAG_OPTION))
          out[response++] = card->locked[i] ? 1 : 0;
        if (command != CMD_SECURITY) {
          memcpy(out + response, card->memory + i * card->block_size, card->block_size);
          response += card->block_size;
        }
      }
      break;
    }
    case CMD_WRITE:
    case CMD_WRITE_MULTIPLE:
    case CMD_LOCK: {
      if (card->is_slix && command == CMD_WRITE_MULTIPLE) {
        error = ERROR_UNSUPPORTED;
        break;
      }
      size_t header = command == CMD_WRITE_MULTIPLE ? 2 : 1;
      if (count < header) {
        error = ERROR_FORMAT;
        break;
      }
      size_t blocks = command == CMD_WRITE_MULTIPLE ? (size_t)args[1] + 1 : 1;
      size_t expected = header + (command == CMD_LOCK ? 0 : blocks * card->block_size);
      if (count != expected) {
        error = ERROR_FORMAT;
        break;
      }
      if ((size_t)args[0] + blocks > card->block_count) {
        error = ERROR_BLOCK;
        break;
      }
      if (card->is_read_only) {
        error = ERROR_LOCKED;
        break;
      }
      if (is_counter(card, args[0])) {
        error = command == CMD_LOCK ? ERROR_LOCKED : write_counter(listener, args + header);
        mutation = error == 0;
        break;
      }
      for (size_t i = args[0]; i < args[0] + blocks; i++)
        if (card->locked[i] || protected_block(listener, i, PASSWORD_READ) ||
            protected_block(listener, i, PASSWORD_WRITE))
          error = ERROR_LOCKED;
      if (error)
        break;
      if (command == CMD_LOCK)
        card->locked[args[0]] = true;
      else
        memcpy(card->memory + args[0] * card->block_size, args + header, blocks * card->block_size);
      mutation = true;
      break;
    }
    case CMD_WRITE_AFI:
    case CMD_LOCK_AFI:
    case CMD_WRITE_DSFID:
    case CMD_LOCK_DSFID: {
      bool afi = command == CMD_WRITE_AFI || command == CMD_LOCK_AFI;
      bool lock = command == CMD_LOCK_AFI || command == CMD_LOCK_DSFID;
      if (count != (lock ? 0U : 1U)) {
        error = ERROR_FORMAT;
        break;
      }
      if (afi && card->is_slix &&
          (card->is_afi_protected || (!card->has_eas && card->has_password[PASSWORD_EAS])) &&
          !listener->authenticated[PASSWORD_EAS]) {
        error = ERROR_LOCKED;
        break;
      }
      if (card->is_read_only || (afi ? card->afi_locked : card->dsfid_locked)) {
        error = ERROR_LOCKED;
        break;
      }
      if (afi) {
        if (lock)
          card->afi_locked = true;
        else
          card->afi = args[0];
      } else {
        if (lock)
          card->dsfid_locked = true;
        else
          card->dsfid = args[0];
      }
      mutation = true;
      break;
    }
    default:
      error = ERROR_UNSUPPORTED;
      break;
  }
  *out_length = error ? error_response(out, error) : finish(out, response);
  if (mutation && (flags & FLAG_OPTION)) {
    memcpy(listener->pending, out, *out_length);
    listener->pending_length = *out_length;
    *out_length = 0;
  }
  return ESP_OK;
}
