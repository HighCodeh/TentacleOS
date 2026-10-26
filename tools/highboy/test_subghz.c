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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "subghz_keeloq.h"
#include "subghz_protocol_registry.h"
#include "subghz_protocol_serializer.h"
#include "subghz_protocol_utils.h"
#include "subghz_replay.h"
#include "storage_assets.h"

#define TEST_PULSE_CAPACITY 256
#define TEST_TEXT_CAPACITY 1024
#define TEST_FREQUENCY_HZ 433920000u
#define TEST_PRESET 6u
#define TEST_KEY UINT64_C(0x0123456789abcdef)
#define TEST_SERIAL 0x1234567u
#define TEST_BUTTON 2u
#define TEST_COUNTER 10u
#define TEST_MAKER "HostFixtureLongName01234567"

static const char MFCODES[] =
    "Manufacturer: " TEST_MAKER "\nKey (Hex): 0123456789ABCDEF\nType: 1\n";
static char s_capture[TEST_TEXT_CAPACITY];
static int32_t s_sent[TEST_PULSE_CAPACITY];
static size_t s_sent_count;
static uint32_t s_frequency;
static bool s_is_running;
static bool s_has_assets;

uint8_t cc1101_get_active_preset_id(void) { return TEST_PRESET; }
esp_err_t cc1101_set_frequency(uint32_t frequency) {
  s_frequency = frequency;
  return ESP_OK;
}
bool subghz_receiver_is_running(void) { return s_is_running; }
void subghz_receiver_stop(void) { s_is_running = false; }
esp_err_t subghz_tx_init(void) { return ESP_OK; }
esp_err_t subghz_tx_send_raw(const int32_t *timings, size_t count) {
  assert(count <= TEST_PULSE_CAPACITY);
  memcpy(s_sent, timings, count * sizeof(*timings));
  s_sent_count = count;
  return ESP_OK;
}
int subghz_storage_read(const char *name, char *out_buf, size_t out_size) {
  (void)name;
  size_t length = strlen(s_capture);
  assert(length < out_size);
  memcpy(out_buf, s_capture, length + 1);
  return (int)length;
}
esp_err_t storage_assets_get_file_size(const char *path, size_t *out_size) {
  assert(strcmp(path, "config/subghz/keeloq_mfcodes.txt") == 0);
  *out_size = s_has_assets ? sizeof(MFCODES) - 1 : 0;
  return s_has_assets ? ESP_OK : ESP_ERR_NOT_FOUND;
}
esp_err_t storage_assets_read_file(const char *path, uint8_t *out, size_t size, size_t *out_read) {
  assert(strcmp(path, "config/subghz/keeloq_mfcodes.txt") == 0);
  assert(s_has_assets && size == sizeof(MFCODES) - 1);
  memcpy(out, MFCODES, size);
  *out_read = size;
  return ESP_OK;
}

static void test_registry(void) {
  char report[TEST_TEXT_CAPACITY];
  subghz_protocol_registry_init();
  assert(subghz_protocol_registry_selftest(report, sizeof(report)));
  assert(strstr(report, "FAIL") == NULL);
  assert(strstr(report, "Holtek_HT12X") != NULL);
  assert(subghz_protocol_registry_selftest(NULL, 0));
  char tiny[2] = {'x', 'x'};
  assert(subghz_protocol_registry_selftest(tiny, sizeof(tiny)));
  assert(tiny[1] == '\0');
  assert(subghz_protocol_registry_get_by_name("CAME 12bit") != NULL);
  assert(subghz_protocol_registry_get_by_name("Nice Flo 12bit") != NULL);
  assert(subghz_protocol_registry_get_by_name("RCSwitch(P1)") != NULL);
  assert(subghz_protocol_registry_get_by_name("KeeLoq " TEST_MAKER) != NULL);
  assert(subghz_protocol_registry_get_by_name("Rossi") == NULL);
  assert(subghz_protocol_registry_get_by_name("CAMEoops") == NULL);
  assert(subghz_protocol_registry_get_by_name("prefix RCSwitch(P1)") == NULL);
  assert(subghz_protocol_registry_get_by_name(NULL) == NULL);
  int32_t pulses[TEST_PULSE_CAPACITY];
  subghz_data_t data = {.protocol_name = "CAME", .bit_count = 12, .raw_value = 0xabc};
  assert(subghz_protocol_registry_encode("CAME", NULL, pulses, TEST_PULSE_CAPACITY) == 0);
  assert(subghz_protocol_registry_encode("CAME", &data, NULL, TEST_PULSE_CAPACITY) == 0);
  assert(subghz_protocol_registry_encode("CAME", &data, pulses, 0) == 0);
  assert(subghz_protocol_registry_encode("unknown", &data, pulses, TEST_PULSE_CAPACITY) == 0);
  for (size_t i = 0; i < TEST_PULSE_CAPACITY; i++) pulses[i] = 123;
  assert(subghz_protocol_registry_encode("CAME", &data, pulses, 24) == 0);
  for (size_t i = 0; i < TEST_PULSE_CAPACITY; i++) assert(pulses[i] == 123);
  data.protocol_name = "RCSwitch(P1)";
  data.bit_count = 33;
  assert(subghz_protocol_registry_encode(data.protocol_name, &data, pulses, TEST_PULSE_CAPACITY) == 0);
  data.bit_count = 0;
  assert(subghz_protocol_registry_encode(data.protocol_name, &data, pulses, TEST_PULSE_CAPACITY) == 0);
  data.bit_count = 24;
  data.protocol_name = "RCSwitch(P-2147483648)";
  assert(subghz_protocol_registry_encode(data.protocol_name, &data, pulses, TEST_PULSE_CAPACITY) == 0);
}

static void test_pwm_vector(void) {
  const int32_t expected[] = {700, -350, 350, -700, 700, -350, -7000};
  int32_t pulses[TEST_PULSE_CAPACITY];
  assert(subghz_pwm_encode(5, 3, 350, 700, pulses, TEST_PULSE_CAPACITY) == 7);
  assert(memcmp(pulses, expected, sizeof(expected)) == 0);
  assert(subghz_pwm_encode(5, 0, 350, 700, pulses, TEST_PULSE_CAPACITY) == 0);
  assert(subghz_pwm_encode(5, 33, 350, 700, pulses, TEST_PULSE_CAPACITY) == 0);
  assert(subghz_pwm_encode(5, 3, 350, 700, pulses, 6) == 0);
}

static void test_serialization_replay(void) {
  subghz_data_t data = {.protocol_name = "Nice Flo 12bit", .bit_count = 12,
                       .serial = 0xabc, .raw_value = 0xabc, .btn = TEST_BUTTON};
  size_t size = subghz_protocol_serialize_decoded(&data, TEST_FREQUENCY_HZ, 700,
                                                s_capture, sizeof(s_capture));
  assert(size == strlen(s_capture));
  assert(strstr(s_capture, "Serial: 2748\nBtn: 2\n") != NULL);
  char tiny[2] = {'x', 'x'};
  assert(subghz_protocol_serialize_decoded(&data, TEST_FREQUENCY_HZ, 700, tiny, sizeof(tiny)) == 0);
  assert(tiny[0] == '\0');
  s_is_running = true;
  assert(subghz_replay_file("fixture") == ESP_OK);
  assert(!s_is_running && s_sent_count != 0 && s_frequency == TEST_FREQUENCY_HZ);
  subghz_data_t decoded = {0};
  const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name(data.protocol_name);
  assert(protocol->decode(s_sent, s_sent_count, &decoded));
  assert(decoded.raw_value == data.raw_value);

  /* Legacy fixed-code captures have no Serial/Btn fields. */
  snprintf(s_capture, sizeof(s_capture),
           "Frequency: %u\nProtocol: CAME 12bit\nBit: 12\nKey: 00 00 00 00 00 00 0A BC\n",
           TEST_FREQUENCY_HZ);
  assert(subghz_replay_file("legacy") == ESP_OK);
  protocol = subghz_protocol_registry_get_by_name("CAME");
  assert(protocol->decode(s_sent, s_sent_count, &decoded) && decoded.raw_value == 0xabc);

  const int32_t raw[] = {350, -700, 700, -350};
  size = subghz_protocol_serialize_raw(raw, 4, TEST_FREQUENCY_HZ, s_capture, sizeof(s_capture));
  assert(size == strlen(s_capture));
  assert(subghz_replay_file("raw") == ESP_OK);
  assert(s_sent_count == 4 && memcmp(s_sent, raw, sizeof(raw)) == 0);
  snprintf(s_capture, sizeof(s_capture), "Protocol: Unknown\nBit: 12\n");
  s_sent_count = 0;
  assert(subghz_replay_file("unknown") == ESP_ERR_NOT_SUPPORTED);
  assert(s_sent_count == 0);
  snprintf(s_capture, sizeof(s_capture),
           "Protocol: CAME 12345678901234567890123456789012345678901234567890\nBit: 12\n");
  assert(subghz_replay_file("overlong") == ESP_ERR_INVALID_ARG);
  assert(s_sent_count == 0);
}

static void test_raw_validation(void) {
  int32_t pulses[4];uint32_t frequency=7;uint8_t preset=8;
  const char *bad[]={"1 2 3 4 5", "350,-700", "350junk", "0", "2147483648",
    "-2147483649", "99999999999999999999999999999", "350 -700\nRAW_Data: 9"};
  for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
    snprintf(s_capture,sizeof(s_capture),"Frequency: 433920000\nPreset: 6\nRAW_Data: %s\n",bad[i]);
    assert(subghz_protocol_parse_raw(s_capture,pulses,4,&frequency,&preset)==0);
    assert(frequency==7 && preset==8);
  }
  const char *bad_header[]={"Frequency: -1\nPreset: 6", "Frequency: 4294967296\nPreset: 6",
    "Frequency: 433920000x\nPreset: 6", "Frequency: 433920000\nPreset: 256",
    "Frequency: 433920000\nPreset: -1", "Frequency: 1\nFrequency: 2\nPreset: 6",
    "XFrequency: 1\nPreset: 6"};
  for(size_t i=0;i<sizeof(bad_header)/sizeof(bad_header[0]);++i) {
    snprintf(s_capture,sizeof(s_capture),"%s\nRAW_Data: 350 -700\n",bad_header[i]);
    assert(subghz_protocol_parse_raw(s_capture,pulses,4,&frequency,&preset)==0);
  }
  const int32_t raw[]={350,-700,700,-350};
  assert(subghz_protocol_serialize_raw(raw,4,TEST_FREQUENCY_HZ,s_capture,sizeof(s_capture))>0);
  size_t length=strlen(s_capture);
  char short_buffer[TEST_TEXT_CAPACITY];
  assert(subghz_protocol_serialize_raw(raw,4,TEST_FREQUENCY_HZ,short_buffer,length)==0);
  assert(short_buffer[0]=='\0');
  assert(subghz_protocol_parse_raw(s_capture,pulses,3,&frequency,&preset)==0);
  assert(subghz_protocol_parse_raw(s_capture,pulses,4,&frequency,&preset)==4);
  assert(frequency==TEST_FREQUENCY_HZ && preset==TEST_PRESET && memcmp(raw,pulses,sizeof(raw))==0);
}

static void test_decoded_validation(void) {
  int32_t pulses[TEST_PULSE_CAPACITY];size_t count=99;uint32_t frequency=7;uint8_t preset=8;
  const char *bad[]={"Frequency: -1", "Frequency: 4294967296", "Bit: 256", "Btn: 256",
    "Serial: -1", "Serial: 4294967296", "Preset: 256", "Key: 00 00 00 00 00 00 00 100",
    "Key: 01 00 00 00 00 00 00 2A", "Key: 00 00 00 00 00 00 00 2A 11"};
  for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
    snprintf(s_capture,sizeof(s_capture),
      "Frequency: 433920000\nProtocol: CAME\nBit: 12\nKey: 00 00 00 00 00 00 00 2A\n%s\n",bad[i]);
    assert(subghz_protocol_parse_capture(s_capture,pulses,TEST_PULSE_CAPACITY,&count,&frequency,&preset)!=ESP_OK);
    assert(count==0 && frequency==7 && preset==8);
    s_sent_count=0;s_is_running=true;
    assert(subghz_replay_file("invalid")!=ESP_OK);
    assert(s_sent_count==0 && s_is_running);
  }
}

static void test_keeloq_vectors(void) {
  /* Synthetic in-memory fixture; no SD manufacturer keys or hardware are read. */
  char report[TEST_TEXT_CAPACITY];
  assert(subghz_keeloq_selftest(report, sizeof(report)));
  assert(strstr(report, "SKIP") != NULL);
  const uint32_t plaintexts[] = {0, 1, 0xdeadbeef, UINT32_MAX};
  for (size_t i = 0; i < sizeof(plaintexts) / sizeof(plaintexts[0]); i++) {
    uint32_t encrypted = subghz_keeloq_encrypt(plaintexts[i], TEST_KEY);
    assert(subghz_keeloq_decrypt(encrypted, TEST_KEY) == plaintexts[i]);
  }
  assert(subghz_keeloq_reverse64(subghz_keeloq_reverse64(TEST_KEY)) == TEST_KEY);
  s_has_assets = true;
  assert(subghz_keeloq_load_mfkeys() == 1);
  assert(subghz_keeloq_mfkey_at(-1) == NULL && subghz_keeloq_mfkey_at(1) == NULL);
  int32_t pulses[TEST_PULSE_CAPACITY];
  size_t count = subghz_keeloq_encode(TEST_SERIAL, TEST_BUTTON, TEST_COUNTER, TEST_KEY,
                                    KL_LEARN_SIMPLE, pulses, TEST_PULSE_CAPACITY);
  assert(count != 0);
  uint32_t fix = 0, hop = 0;
  assert(subghz_keeloq_pwm_decode(pulses, count, &fix, &hop));
  keeloq_result_t result = {0};
  assert(subghz_keeloq_decode_key(((uint64_t)fix << 32) | hop, &result));
  assert(result.serial == TEST_SERIAL && result.button == TEST_BUTTON);
  assert(result.counter == TEST_COUNTER && strcmp(result.mfname, TEST_MAKER) == 0);
  subghz_data_t data = {.protocol_name = "KeeLoq " TEST_MAKER, .bit_count = 64,
                       .serial = TEST_SERIAL, .btn = TEST_BUTTON, .raw_value = TEST_COUNTER};
  assert(subghz_protocol_serialize_decoded(&data, TEST_FREQUENCY_HZ, 400,
                                           s_capture, sizeof(s_capture)) != 0);
  assert(subghz_replay_file("synthetic") == ESP_OK);
  assert(subghz_keeloq_pwm_decode(s_sent, s_sent_count, &fix, &hop));
  assert(subghz_keeloq_decode_key(((uint64_t)fix << 32) | hop, &result));
  assert(result.serial == TEST_SERIAL && result.button == TEST_BUTTON);
  assert(result.counter == TEST_COUNTER + 1);
}

int main(void) {
  test_registry();
  test_pwm_vector();
  test_serialization_replay();
  test_raw_validation();
  test_decoded_validation();
  test_keeloq_vectors();
  puts("PASS: 12 protocol round-trips, lookup/bounds, fixed PWM vector, serialization/replay,");
  puts("      cipher/frame vectors and synthetic long-name KeeLoq replay (no physical radio).");
  return 0;
}
