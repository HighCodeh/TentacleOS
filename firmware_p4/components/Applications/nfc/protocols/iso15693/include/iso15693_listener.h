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
#ifndef ISO15693_LISTENER_H
#define ISO15693_LISTENER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ISO15693_LISTENER_BLOCKS      256U
#define ISO15693_LISTENER_BLOCK_BYTES 32U
#define ISO15693_LISTENER_REPLY_BYTES 1024U
#define ISO15693_LISTENER_PASSWORDS   5U

typedef struct {
  uint8_t uid[8];
  uint8_t dsfid, afi, ic_ref;
  uint16_t block_count;
  uint8_t block_size;
  uint8_t memory[ISO15693_LISTENER_BLOCKS * ISO15693_LISTENER_BLOCK_BYTES];
  bool locked[ISO15693_LISTENER_BLOCKS];
  bool is_read_only, is_slix, is_private, afi_locked, dsfid_locked;
  bool has_password[ISO15693_LISTENER_PASSWORDS];
  uint8_t passwords[ISO15693_LISTENER_PASSWORDS][4];
  bool password_locked[ISO15693_LISTENER_PASSWORDS];
  uint8_t signature[32];
  bool has_signature;
  uint8_t protection_pointer, protection_condition;
  bool protection_locked, eas_locked, has_system_info;
  bool has_eas, is_eas_enabled, is_eas_protected, is_afi_protected;
  bool is_password_64, is_destroyed;
  uint8_t eas_id[2];
} iso15693_listener_card_t;

typedef struct {
  iso15693_listener_card_t *card;
  bool is_selected, is_quiet, has_nonce;
  bool authenticated[ISO15693_LISTENER_PASSWORDS];
  uint8_t nonce[2];
  uint8_t inventory_slot, current_slot;
  bool has_inventory;
  uint8_t pending[4];
  size_t pending_length;
  bool is_high_rate, is_fast_rate;
  uint8_t inventory_reply[ISO15693_LISTENER_REPLY_BYTES];
  size_t inventory_reply_length;
  uint8_t inventory_options;
  bool is_persistent_quiet;
  uint64_t field_off_us;
} iso15693_listener_t;

esp_err_t iso15693_listener_init(iso15693_listener_t *listener, iso15693_listener_card_t *card);
void iso15693_listener_reset(iso15693_listener_t *listener);
void iso15693_listener_field(iso15693_listener_t *listener, bool is_present, uint64_t now_us);
/* Full wire frames include CRC. A slot marker has length zero. Silent requests
 * return ESP_OK with length zero. Card mutations affect caller-owned RAM only. */
esp_err_t iso15693_listener_exchange(iso15693_listener_t *listener,
                                     const uint8_t *frame,
                                     size_t length,
                                     bool is_slot,
                                     uint8_t *out,
                                     size_t capacity,
                                     size_t *out_length);

#ifdef __cplusplus
}
#endif
#endif
