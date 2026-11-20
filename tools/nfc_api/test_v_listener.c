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

#include <assert.h>
#include <stdio.h>
#include <string.h>

static iso15693_listener_card_t card;
static iso15693_listener_t listener;
static uint8_t out[ISO15693_LISTENER_REPLY_BYTES];
static uint8_t wire[1024];
static size_t length;

void esp_fill_random(void *buffer, size_t size) {
  assert(size == 2);
  memcpy(buffer, "\x12\x34", size);
}
static uint16_t crc(const uint8_t *data, size_t size) {
  uint16_t value = 0xffff;
  for (size_t i = 0; i < size; i++) {
    value ^= data[i];
    for (unsigned bit = 0; bit < 8; bit++)
      value = (value >> 1) ^ ((value & 1) ? 0x8408 : 0);
  }
  return (uint16_t)~value;
}
static size_t request(const uint8_t *data, size_t size) {
  assert(size + 2 <= sizeof(wire));
  memcpy(wire, data, size);
  uint16_t value = crc(wire, size);
  wire[size] = value; wire[size + 1] = value >> 8;
  assert(iso15693_listener_exchange(&listener, wire, size + 2, false,
                                    out, sizeof(out), &length) == ESP_OK);
  if (length) assert(crc(out, length - 2) == (uint16_t)(out[length-2] | out[length-1] << 8));
  return length;
}
static size_t slot(void) {
  assert(iso15693_listener_exchange(&listener, NULL, 0, true, out, sizeof(out), &length) == ESP_OK);
  return length;
}
static size_t addressed(uint8_t command, const uint8_t *args, size_t count) {
  uint8_t frame[64] = {0x22, command};
  memcpy(frame + 2, card.uid, 8);
  if (count) memcpy(frame + 10, args, count);
  return request(frame, 10 + count);
}
static size_t custom_addressed(uint8_t command, const uint8_t *args, size_t count) {
  uint8_t frame[64] = {0x22, command, 4};
  memcpy(frame + 3, card.uid, 8);
  if (count) memcpy(frame + 11, args, count);
  return request(frame, 11 + count);
}

static void authenticate(unsigned index) {
  const uint8_t random[] = {2, 0xb2, 4};
  uint8_t auth[] = {2, 0xb3, 4, (uint8_t)(1U << index), 0x13, 0x36, 0x11, 0x30};
  card.has_password[index] = true;
  memcpy(card.passwords[index], "\x01\x02\x03\x04", 4);
  assert(request(random, sizeof(random)) == 5);
  assert(request(auth, sizeof(auth)) == 3);
}

static void inventory_read_tests(void) {
  memset(&card, 0, sizeof(card));
  card.is_slix = true; card.block_count = 80; card.block_size = 4;
  card.uid[0] = 3; card.uid[7] = 0xe0;
  for (unsigned i = 0; i < 320; i++) card.memory[i] = (uint8_t)i;
  assert(iso15693_listener_init(&listener, &card) == ESP_OK);
  const uint8_t single[] = {0x26, 0xa0, 4, 0, 0, 1};
  assert(request(single, sizeof(single)) == 11 && out[1] == 0 && out[8] == 7);
  const uint8_t multiple[] = {0x46, 0xa1, 4, 0, 1, 0};
  assert(request(multiple, sizeof(multiple)) == 0 && listener.is_fast_rate);
  assert(slot() == 0 && slot() == 0 && slot() == 15);
  assert(out[1] == 3 && out[8] == 0xe0 && out[9] == 4 && out[12] == 7);
  const uint8_t partial_uid[] = {0x66, 0xa0, 4, 8, 3, 79, 4};
  assert(request(partial_uid, sizeof(partial_uid)) == 14 && out[7] == 0xe0 && out[8] == 60);
  const uint8_t extended[] = {0x66, 0xa0, 4, 0x80, 0x12};
  assert(request(extended, sizeof(extended)) == 11 && out[1] == 3);
  const uint8_t eas_filter[] = {0x26, 0xa0, 4, 0x80, 0x11};
  assert(request(eas_filter, sizeof(eas_filter)) == 0);
  card.has_eas = card.is_eas_enabled = true;
  assert(request(eas_filter, sizeof(eas_filter)) == 3);
  const uint8_t quiet_after[] = {0x26, 0xa0, 4, 0x80, 0x30};
  assert(request(quiet_after, sizeof(quiet_after)) == 3 && listener.is_quiet);
  assert(request(single, sizeof(single)) == 0);
  iso15693_listener_reset(&listener);
  const uint8_t persistent_after[] = {0x26, 0xa0, 4, 0x80, 0x50};
  assert(request(persistent_after, sizeof(persistent_after)) == 3 && listener.is_persistent_quiet);
  const uint8_t only_persistent[] = {0x26, 0xa0, 4, 0x80, 0x70};
  assert(request(only_persistent, sizeof(only_persistent)) == 3);
  iso15693_listener_reset(&listener);
  assert(request(only_persistent, sizeof(only_persistent)) == 0);
  const uint8_t wrong_vendor[] = {0x26, 0xa0, 5, 0, 0, 0};
  assert(request(wrong_vendor, sizeof(wrong_vendor)) == 0);
  for (size_t i = 0; i < sizeof(single); i++) assert(request(single, i) == 0);
  card.has_system_info = true; card.protection_condition = 0x10;
  assert(request(single, sizeof(single)) == 0);
  authenticate(0);
  assert(request(single, sizeof(single)) == 11);
  assert(addressed(0x25, NULL, 0) == 3);
  authenticate(0);
  const uint8_t selected[] = {0x12, 0xa0, 4, 0, 0, 0};
  assert(request(selected, sizeof(selected)) == 7);
  uint8_t addressed_read[] = {0x22, 0xa0, 4, 64, 3, 0, 0, 0, 0, 0, 0, 0xe0, 0, 0};
  assert(request(addressed_read, sizeof(addressed_read)) == 7);
  addressed_read[4] ^= 1;
  assert(request(addressed_read, sizeof(addressed_read)) == 0);
}

static void extended_slix(void) {
  memset(&card, 0, sizeof(card));
  card.is_slix = true; card.block_count = 80; card.block_size = 4;
  card.has_system_info = true; card.has_eas = true;
  assert(iso15693_listener_init(&listener, &card) == ESP_OK);
  uint8_t block = 79;
  card.protection_condition = 0x33;
  assert(addressed(0x20, &block, 1) == 7);
  uint8_t increment[] = {79, 1, 0, 0, 0};
  assert(addressed(0x21, increment, sizeof(increment)) == 3);
  assert(card.memory[316] == 1);
  assert(addressed(0x22, &block, 1) == 4 && !card.locked[79]);
  card.memory[319] = 1;
  assert(addressed(0x21, increment, sizeof(increment)) == 4);
  authenticate(0);
  assert(addressed(0x21, increment, sizeof(increment)) == 3 && card.memory[316] == 2);
  uint8_t preset[] = {79, 0xfe, 0xff, 0, 1};
  assert(addressed(0x21, preset, sizeof(preset)) == 4);
  authenticate(1);
  assert(addressed(0x21, preset, sizeof(preset)) == 3);
  assert(addressed(0x21, increment, sizeof(increment)) == 3);
  assert(card.memory[316] == 0xff && card.memory[317] == 0xff);
  assert(addressed(0x21, increment, sizeof(increment)) == 3);
  assert(card.memory[316] == 0 && card.memory[317] == 0 && card.memory[319] == 1);
  preset[3] = 1;
  assert(addressed(0x21, preset, sizeof(preset)) == 4);
  card.is_read_only = true;
  assert(addressed(0x21, increment, sizeof(increment)) == 4);
  card.is_read_only = false;

  assert(custom_addressed(0xa5, NULL, 0) == 0);
  assert(custom_addressed(0xa2, NULL, 0) == 3);
  assert(custom_addressed(0xa5, NULL, 0) == 35 && out[1] == 0x2f && out[32] == 0xef);
  uint8_t id[] = {0x12, 0x34};
  assert(custom_addressed(0xa7, id, sizeof(id)) == 3);
  uint8_t alarm[] = {0x42, 0xa5, 4, 0};
  assert(request(alarm, sizeof(alarm)) == 5 && out[1] == 0x12 && out[2] == 0x34);
  uint8_t mask[] = {0x42, 0xa5, 4, 8, 0x12};
  assert(request(mask, sizeof(mask)) == 35);
  mask[4] ^= 1; assert(request(mask, sizeof(mask)) == 0);
  mask[3] = 7; assert(request(mask, sizeof(mask)) == 0);
  assert(custom_addressed(0xa6, NULL, 0) == 4);
  authenticate(4);
  assert(custom_addressed(0xa6, NULL, 0) == 3 && card.is_eas_protected);
  const uint8_t protect_afi[] = {0x42, 0xa6, 4};
  assert(request(protect_afi, sizeof(protect_afi)) == 3 && card.is_afi_protected);
  iso15693_listener_reset(&listener);
  assert(custom_addressed(0xa3, NULL, 0) == 4 && card.is_eas_enabled);
  uint8_t afi = 7;
  assert(addressed(0x27, &afi, 1) == 4);
  authenticate(4);
  const uint8_t reset_option[] = {0x42, 0xa3, 4};
  assert(request(reset_option, sizeof(reset_option)) == 0 && !card.is_eas_enabled);
  assert(slot() == 3);
  assert(custom_addressed(0xa2, NULL, 0) == 3);
  assert(custom_addressed(0xa4, NULL, 0) == 3 && card.eas_locked);
  assert(custom_addressed(0xa3, NULL, 0) == 4);
  assert(custom_addressed(0xa7, id, sizeof(id)) == 4);

  authenticate(0); authenticate(1);
  uint8_t pw_id = 1;
  assert(custom_addressed(0xb5, &pw_id, 1) == 3 && card.password_locked[0]);
  uint8_t password[] = {1, 9, 9, 9, 9};
  assert(custom_addressed(0xb4, password, sizeof(password)) == 4);
  assert(custom_addressed(0xbb, NULL, 0) == 3 && card.is_password_64);
  card.protection_condition = 1;
  card.protection_pointer = 16;
  iso15693_listener_reset(&listener);
  authenticate(0); block = 0;
  assert(addressed(0x20, &block, 1) == 4);
  authenticate(1);
  assert(addressed(0x20, &block, 1) == 7);
  uint8_t pointer = 1;
  assert(custom_addressed(0xb7, &pointer, 1) == 4);
  pointer = 16;
  assert(custom_addressed(0xb7, &pointer, 1) == 3 && card.protection_locked);

  const uint8_t inv[] = {0x26, 1, 0};
  assert(custom_addressed(0xbc, NULL, 0) == 0 && listener.is_persistent_quiet);
  assert(request(inv, sizeof(inv)) == 0);
  const uint8_t alarm_plain[] = {2, 0xa5, 4};
  assert(request(alarm_plain, sizeof(alarm_plain)) == 35);
  iso15693_listener_field(&listener, false, 100);
  iso15693_listener_field(&listener, true, 1000100);
  assert(listener.is_persistent_quiet && !listener.authenticated[0]);
  iso15693_listener_field(&listener, false, 2000100);
  iso15693_listener_field(&listener, true, 4000100);
  assert(!listener.is_persistent_quiet && request(inv, sizeof(inv)) == 12);
  assert(custom_addressed(0xbc, NULL, 0) == 0);
  assert(addressed(0x26, NULL, 0) == 3 && !listener.is_persistent_quiet);
  assert(custom_addressed(0xbc, NULL, 0) == 0);
  assert(addressed(0x25, NULL, 0) == 3 && !listener.is_persistent_quiet);

  authenticate(3);
  const uint8_t random[] = {2, 0xb2, 4};
  const uint8_t destroy[] = {0x13, 0x36, 0x11, 0x30};
  assert(request(random, sizeof(random)) == 5);
  assert(custom_addressed(0xb9, destroy, sizeof(destroy)) == 3 && card.is_destroyed);
  iso15693_listener_reset(&listener);
  assert(request(inv, sizeof(inv)) == 0 && custom_addressed(0xa5, NULL, 0) == 0);
}

int main(void) {
  const uint8_t uid[] = {3,2,3,4,5,6,4,0xe0};
  memcpy(card.uid, uid, sizeof(uid));
  card.block_count = 32; card.block_size = 4; card.is_slix = true;
  for (unsigned i = 0; i < 128; i++) card.memory[i] = i;
  assert(iso15693_listener_init(&listener, &card) == ESP_OK);
  const uint8_t inv[] = {0x26,1,0};
  assert(request(inv,sizeof(inv)) == 12 && memcmp(out+2,uid,8)==0);
  const uint8_t inv16[] = {0x06,1,0};
  assert(request(inv16,sizeof(inv16)) == 0);
  assert(slot()==0 && slot()==0 && slot()==12);
  for (unsigned i=0;i<20;i++) assert(slot()==0);
  const uint8_t mismatch[] = {0x26,1,4,7};
  assert(request(mismatch,sizeof(mismatch))==0);
  const uint8_t match[] = {0x26,1,4,3};
  assert(request(match,sizeof(match))==12);
  uint8_t block=1;
  assert(addressed(0x20,&block,1)==7 && out[1]==4);
  uint8_t selected[]={0x12,0x20,1};
  assert(request(selected,sizeof(selected))==0);
  assert(addressed(0x25,NULL,0)==3);
  assert(request(selected,sizeof(selected))==7);
  assert(addressed(0x02,NULL,0)==0);
  assert(request(inv,sizeof(inv))==0);
  assert(addressed(0x26,NULL,0)==3);
  assert(request(inv,sizeof(inv))==12);
  uint8_t write[]={0x42,0x21,1,0xaa,0xbb,0xcc,0xdd};
  assert(request(write,sizeof(write))==0 && card.memory[4]==0xaa);
  wire[sizeof(write)] ^= 1;
  assert(iso15693_listener_exchange(&listener, wire, sizeof(write) + 2, false,
                                    out, sizeof(out), &length) == ESP_OK);
  assert(length == 0);
  assert(slot()==3 && slot()==0);
  assert(addressed(0x22,&block,1)==3 && card.locked[1]);
  assert(request(write,sizeof(write))==4 && out[1]==0x12);
  uint8_t multi[]={0x42,0x23,0,1};
  assert(request(multi,sizeof(multi))==13 && out[1]==0 && out[6]==1);
  uint8_t range[]={31,1};
  assert(addressed(0x23,range,2)==4 && out[1]==0x10);
  uint8_t security[]={0,1};
  assert(addressed(0x2c,security,2)==5 && out[1]==0 && out[2]==1);
  assert(addressed(0x2b,NULL,0)==17 && out[12]==31 && out[13]==3);
  uint8_t afi=0x12;
  assert(addressed(0x27,&afi,1)==3 && card.afi==afi);
  assert(addressed(0x28,NULL,0)==3);
  afi=0x34; assert(addressed(0x27,&afi,1)==4 && card.afi==0x12);
  card.is_read_only=true;
  uint8_t plain_write[]={0x02,0x21,0,9,9,9,9};
  assert(request(plain_write,sizeof(plain_write))==4 && card.memory[0]==0);
  card.is_read_only=false;
  request(plain_write,sizeof(plain_write)); assert(card.memory[0]==9);
  wire[3]^=1;
  assert(iso15693_listener_exchange(&listener,wire,sizeof(plain_write)+2,false,out,sizeof(out),&length)==0);
  assert(length==0 && card.memory[0]==9);
  const uint8_t random[]={2,0xb2,4};
  assert(request(random,sizeof(random))==5 && out[1]==0x12 && out[2]==0x34);
  uint8_t auth[]={2,0xb3,4,4,0x13,0x36,0x11,0x30};
  card.has_password[2]=true;
  const uint8_t password[]={1,2,3,4}; memcpy(card.passwords[2],password,4);
  card.is_private=true;
  assert(request(inv,sizeof(inv))==0);
  assert(request(auth,sizeof(auth))==3 && !card.is_private);
  assert(request(auth,sizeof(auth))==4);
  assert(request(random,sizeof(random))==5);
  auth[4]^=1;
  assert(request(auth,sizeof(auth))==0 && !listener.authenticated[2]);
  assert(request(random,sizeof(random))==5);
  const uint8_t privacy[]={2,0xba,4,0x13,0x36,0x11,0x30};
  assert(request(privacy,sizeof(privacy))==3 && card.is_private);
  iso15693_listener_reset(&listener);
  assert(!listener.has_nonce && !listener.authenticated[2] && card.is_private);
  card.is_private=false;
  memset(card.signature,0xa5,sizeof(card.signature)); card.has_signature=true;
  const uint8_t signature[]={2,0xbd,4};
  assert(request(signature,sizeof(signature))==35 && out[1]==0xa5);
  const uint8_t unsupported[]={2,0xb8,4};
  assert(request(unsupported,sizeof(unsupported))==4 && out[1]==1);
  card.has_system_info = true;
  card.protection_pointer = 16;
  card.protection_condition = 0x03;
  block = 0;
  assert(addressed(0x20, &block, 1) == 4);
  assert(request(plain_write, sizeof(plain_write)) == 4);
  block = 16;
  assert(addressed(0x20, &block, 1) == 7);
  card.has_password[0] = card.has_password[1] = true;
  memcpy(card.passwords[0], password, 4);
  memcpy(card.passwords[1], password, 4);
  auth[3] = 1; auth[4] = 0x13;
  assert(request(random, sizeof(random)) == 5);
  assert(request(auth, sizeof(auth)) == 3);
  block = 0;
  assert(addressed(0x20, &block, 1) == 7);
  assert(request(plain_write, sizeof(plain_write)) == 4);
  const uint8_t protect[] = {2, 0xb6, 4, 8, 0x30};
  assert(request(protect, sizeof(protect)) == 4);
  auth[3] = 2;
  assert(request(random, sizeof(random)) == 5);
  assert(request(auth, sizeof(auth)) == 3);
  assert(request(plain_write, sizeof(plain_write)) == 3);
  assert(request(protect, sizeof(protect)) == 3);
  assert(card.protection_pointer == 8 && card.protection_condition == 0x30);
  card.protection_locked = true;
  assert(request(protect, sizeof(protect)) == 4);
  const uint8_t system[] = {2, 0xab, 4};
  assert(request(system, sizeof(system)) == 10 && out[1] == 8 && out[2] == 0x30);
  iso15693_listener_reset(&listener);
  block = 16;
  assert(addressed(0x20, &block, 1) == 4);
  block = 0;
  assert(addressed(0x20, &block, 1) == 7);
  const uint8_t write_multiple[] = {2, 0x24, 0, 0, 1, 2, 3, 4};
  assert(request(write_multiple, sizeof(write_multiple)) == 4 && out[1] == 1);
  for (size_t i=0;i<sizeof(plain_write);i++) request(plain_write,i);
  assert(iso15693_listener_exchange(&listener,wire,4,false,out,1,&length)==ESP_ERR_INVALID_ARG);
  inventory_read_tests();
  extended_slix();
  puts("NFC-V listener: inventory slots/masks, states, blocks/locks, CRC, SLIX nonce/privacy and bounds OK");
}
