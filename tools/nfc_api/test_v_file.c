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

#include "nfc_v_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static iso15693_listener_card_t card, saved;
static bool fail_write;
static iso15693_listener_card_t s_v_card;
static bool s_has_v, s_v_result_valid;
static struct { bool running; int operation; } s_status;
#define NFC_PROTOCOL_OP_V_EMULATE 1
#define portENTER_CRITICAL(mux) ((void)0)
#define portEXIT_CRITICAL(mux) ((void)0)
/* SNAPSHOT */
int storage_write_atomic(const char *path, const void *data, size_t length) {
  if (fail_write) return ESP_ERR_NO_MEM;
  FILE *file = fopen(path, "wb");
  assert(file);
  assert(fwrite(data, 1, length, file) == length);
  assert(fclose(file) == 0);
  return ESP_OK;
}

static void roundtrip(const char *path) {
  assert(nfc_v_file_save(path, &card) == ESP_OK);
  assert(nfc_v_file_load(path, &saved) == ESP_OK);
  card.is_read_only = true;
  assert(!memcmp(&card, &saved, sizeof(card)));
}
int main(int argc, char **argv) {
  assert(argc >= 3);
  assert(nfc_protocol_service_get_v_result(NULL) == ESP_ERR_INVALID_ARG);
  assert(nfc_protocol_service_get_v_result(&saved) == ESP_ERR_INVALID_STATE);
  s_has_v = true;s_status.operation = NFC_PROTOCOL_OP_V_EMULATE;
  assert(nfc_protocol_service_get_v_result(&saved) == ESP_ERR_INVALID_STATE);
  s_v_result_valid = true;s_status.running = true;
  assert(nfc_protocol_service_get_v_result(&saved) == ESP_ERR_INVALID_STATE);
  s_status.running = false;s_v_card.memory[0] = 42;
  assert(nfc_protocol_service_get_v_result(&saved) == ESP_OK && saved.memory[0] == 42);
  saved.memory[0] = 9;assert(s_v_card.memory[0] == 42);
  s_status.operation = 0;
  assert(nfc_protocol_service_get_v_result(&saved) == ESP_ERR_INVALID_STATE);
  assert(nfc_v_file_load(argv[1], &card) == ESP_OK);
  assert(card.is_slix && card.is_read_only && card.block_count == 2 && card.block_size == 4);
  assert(card.uid[0] == 0x81 && card.uid[7] == 0xe0);
  assert(card.memory[7] == 7 && card.locked[1] && !card.locked[0]);
  assert(card.has_password[2] && !card.has_password[0]);
  assert(card.passwords[2][3] == 4 && card.has_system_info);
  assert(card.has_eas && card.is_eas_enabled && card.eas_id[0] == 0x12 && card.eas_id[1] == 0x34);
  assert(card.is_eas_protected && !card.is_afi_protected);
  assert(card.is_password_64 && card.password_locked[1] && !card.password_locked[0]);
  char path[1024];
  snprintf(path, sizeof(path), "%s.saved", argv[1]);
  card.is_private = true;card.is_destroyed = true;
  card.memory[0] = 42;card.locked[0] = true;
  card.has_signature = true;memset(card.signature, 0xBC, sizeof(card.signature));
  roundtrip(path);
  card.has_system_info = false;card.protection_pointer = 0;card.protection_condition = 0;
  card.protection_locked = false;card.eas_locked = true;
  roundtrip(path);
  fail_write = true;assert(nfc_v_file_save(path, &card) == ESP_ERR_NO_MEM);fail_write = false;
  memset(&card, 0, sizeof(card));
  card.uid[7] = 0xE0;card.block_count = ISO15693_LISTENER_BLOCKS;
  card.block_size = ISO15693_LISTENER_BLOCK_BYTES;
  for (size_t i = 0; i < sizeof(card.memory); i++) card.memory[i] = (uint8_t)i;
  for (size_t i = 0; i < card.block_count; i++) card.locked[i] = i % 2;
  roundtrip(path);
  card.block_count++;assert(nfc_v_file_save(path, &card) == ESP_ERR_INVALID_ARG);
  remove(path);
  for (int i = 2; i < argc; i++) {
    memset(&card, 0xa5, sizeof(card));
    assert(nfc_v_file_load(argv[i], &card) != ESP_OK);
    const unsigned char *bytes = (const unsigned char *)&card;
    for (size_t n = 0; n < sizeof(card); n++) assert(bytes[n] == 0);
  }
  puts("NFC-V import: metadata, UID order, keys, locks and malformed files OK");
}
