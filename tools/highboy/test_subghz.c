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
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "subghz_keeloq.h"
#include "flipper_subghz.h"
#include "subghz_protocol_registry.h"
#include "subghz_protocol_serializer.h"
#include "subghz_protocol_utils.h"
#include "subghz_replay.h"
#include "subghz_transmitter.h"
#include "subghz_storage.h"
#include "tos_storage_paths.h"
#include <utime.h>
#include <dirent.h>
#include "storage_assets.h"

#define TEST_PULSE_CAPACITY 8192
#define TEST_TEXT_CAPACITY 65536
#define TEST_FREQUENCY_HZ 433920000u
#define TEST_PRESET 6u
#define TEST_KEY UINT64_C(0x0123456789abcdef)
#define TEST_SERIAL 0x1234567u
#define TEST_BUTTON 2u
#define TEST_COUNTER 10u
#define TEST_MAKER "HostFixtureLongName01234567"
#define TEST_NATIVE_REPEATS 5U

static const char MFCODES[] =
    "Manufacturer: " TEST_MAKER "\nKey (Hex): 0123456789ABCDEF\nType: 1\n";
static char s_capture[TEST_TEXT_CAPACITY];
static int32_t s_sent[TEST_PULSE_CAPACITY];
static size_t s_sent_count;
static uint32_t s_frequency;
static cc1101_preset_t s_sent_preset;
static bool s_is_running;
static bool s_has_assets;

static bool s_has_custom_radio;
static cc1101_radio_config_t s_custom_radio, s_sent_radio;
uint8_t cc1101_get_active_preset_id(void) { return TEST_PRESET; }
esp_err_t cc1101_get_radio_config(cc1101_radio_config_t *out) {
  if (s_has_custom_radio) { *out=s_custom_radio; return ESP_OK; }
  return cc1101_config_from_preset(TEST_PRESET, out);
}
esp_err_t cc1101_set_frequency(uint32_t frequency) {
  s_frequency = frequency;
  return ESP_OK;
}
bool subghz_receiver_is_running(void) { return s_is_running; }
void subghz_receiver_stop(void) { s_is_running = false; }
esp_err_t subghz_tx_init(void) { return ESP_OK; }
esp_err_t subghz_tx_send_configured(const int32_t *timings, size_t count,
                                    const subghz_tx_config_t *config) {
  assert(config != NULL);
  s_frequency = config->frequency_hz;
  s_sent_preset = config->preset;
  assert(config->has_radio_config); s_sent_radio = config->radio;
  return subghz_tx_send_raw(timings, count);
}
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
esp_err_t subghz_tx_send_file(subghz_raw_file_t *file, const subghz_tx_config_t *config) {
  subghz_raw_reader_t reader = {0};
  esp_err_t error = subghz_raw_reader_open(file, &reader);
  int32_t pulses[TEST_PULSE_CAPACITY]; size_t count = 0;
  while (error == ESP_OK) {
    int32_t pulse; bool eof;
    error = subghz_raw_reader_next(&reader, &pulse, &eof);
    if (error != ESP_OK || eof) break;
    assert(count < TEST_PULSE_CAPACITY);
    pulses[count++] = pulse;
  }
  subghz_raw_reader_close(&reader);
  return error == ESP_OK ? subghz_tx_send_configured(pulses, count, config) : error;
}
static esp_err_t replay_fixture(const char *name) {
  char path[256]; snprintf(path, sizeof(path), TOS_PATH_SUBGHZ "/%s.sub", name);
  FILE *file = fopen(path, "wb"); assert(file);
  assert(fwrite(s_capture, 1, strlen(s_capture), file) == strlen(s_capture));
  assert(fclose(file) == 0);
  esp_err_t error = subghz_replay_file(name);
  assert(remove(path) == 0);
  return error;
}
static void test_stream_file(void) {
  cc1101_radio_config_t radio;
  assert(cc1101_config_from_preset(CC1101_PRESET_OOK_270KHZ, &radio) == ESP_OK);
  subghz_raw_file_t *file = NULL;
  assert(subghz_raw_file_create(TEST_FREQUENCY_HZ, &radio, &file) == ESP_OK);
  int32_t pulses[512];
  for (size_t i = 0; i < 512; ++i) pulses[i] = (i & 1) ? -(int32_t)(i + 1) : (int32_t)(i + 1);
  for (unsigned batch = 0; batch < 256; ++batch)
    assert(subghz_raw_file_append(file, pulses, 512) == ESP_OK);
  assert(subghz_raw_file_finish(file) == ESP_OK);
  subghz_raw_reader_t reader;
  assert(subghz_raw_reader_open(file, &reader) == ESP_OK);
  for (unsigned pass = 0; pass < 2; ++pass) {
    for (unsigned i = 0; i < 131072; ++i) {
      int32_t pulse; bool eof;
      assert(subghz_raw_reader_next(&reader, &pulse, &eof) == ESP_OK && !eof);
      assert(pulse == pulses[i % 512]);
    }
    int32_t pulse; bool eof;
    assert(subghz_raw_reader_next(&reader, &pulse, &eof) == ESP_OK && eof);
    assert(subghz_raw_reader_rewind(&reader) == ESP_OK);
  }
  subghz_raw_reader_close(&reader);
  assert(subghz_raw_file_save(file, "../invalid") == ESP_ERR_INVALID_ARG);
  assert(subghz_raw_file_save(file, "stream") == ESP_OK);
  subghz_raw_file_release(file);
  assert(subghz_raw_file_load("stream", &file) == ESP_OK);
  assert(subghz_raw_reader_open(file, &reader) == ESP_OK);
  int32_t pulse; bool eof;
  assert(subghz_raw_reader_next(&reader, &pulse, &eof) == ESP_OK && pulse == 1);
  subghz_raw_reader_close(&reader);
  subghz_raw_file_release(file);
  assert(remove(TOS_PATH_SUBGHZ "/stream.sub") == 0);
  assert(subghz_raw_file_create(TEST_FREQUENCY_HZ, &radio, &file) == ESP_OK);
  assert(subghz_raw_file_append(file, pulses, 512) == ESP_OK);
  assert(subghz_raw_file_finish(file) == ESP_OK);
  subghz_raw_file_acquire(file);
  subghz_raw_file_release(file);
  assert(subghz_raw_reader_open(file, &reader) == ESP_OK);
  subghz_raw_reader_close(&reader);
  subghz_raw_file_release(file);
  DIR *directory = opendir(TOS_PATH_SUBGHZ); assert(directory);
  struct dirent *entry;
  while ((entry = readdir(directory))) assert(strncmp(entry->d_name, ".raw-", 5) != 0);
  assert(closedir(directory) == 0);
  puts("PASS: 131072 RAW pulses, exact signed streaming/rewind, Save and retained temporary ownership");
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

static void test_catalog(void) {
  assert(flipper_subghz_count() == 68);
  for (size_t i = 0; i < flipper_subghz_count(); ++i) {
    flipper_subghz_protocol_info_t info;
    assert(flipper_subghz_info(i, &info));
    const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name(info.name);
    assert(protocol != NULL && protocol->decode != NULL);
    assert((protocol->encode != NULL) == info.can_encode);
  }
  assert(subghz_protocol_registry_get_by_name("RCSwitch") == NULL);
  int32_t pulses[2048];
  flipper_subghz_result_t result;
  const char *fields = "Protocol: Hormann HSM\nBit: 44\nKey: 00 00 0F E0 DE F0 12 33\n";
  size_t count = 0;
  assert(flipper_subghz_encode(fields, pulses, 2048, &count) == ESP_OK);
  assert(flipper_subghz_decode_named("Hormann HSM", pulses, count, &result) == ESP_OK);
  assert(result.bits == 44 && result.key == UINT64_C(0xFE0DEF01233));
  assert(strstr(result.fields, "Key: 00 00 0F E0 DE F0 12 33") != NULL);
  const char *wide = "Protocol: Hormann BiSecur\nBit: 176\nKey: 50 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15\n";
  assert(flipper_subghz_encode(wide, pulses, 2048, &count) == ESP_OK && count > 176);
  subghz_data_t data = {.protocol_name = "Hormann BiSecur", .bit_count = 176};
  snprintf(data.protocol_fields, sizeof(data.protocol_fields), "%s", wide);
  assert(subghz_protocol_serialize_decoded(&data, TEST_FREQUENCY_HZ, 0, s_capture, sizeof(s_capture)) > 0);
  assert(strstr(s_capture, wide) != NULL);
  assert(replay_fixture("wide-key") == ESP_OK);
  uint32_t random = 1234567;
  for (size_t sample = 0; sample < 8; ++sample) {
    for (size_t i = 0; i < 2048; ++i) {
      random = random * 1664525U + 1013904223U;
      int32_t duration = (int32_t)(random % 40000U + 1U);
      pulses[i] = i % 2 ? -duration : duration;
    }
    esp_err_t error = flipper_subghz_decode(pulses, 2048, &result);
    assert(error == ESP_OK || error == ESP_ERR_NOT_FOUND);
  }
  puts("PASS: 68-entry catalog/capabilities, 44-bit round-trip, 176-bit file replay, noisy bursts");
}

static void test_registry(void) {
  char report[TEST_TEXT_CAPACITY];
  subghz_protocol_registry_init();
  assert(subghz_protocol_registry_selftest(report, sizeof(report)));
  assert(strstr(report, "FAIL") == NULL);
  assert(strstr(report, "Princeton") != NULL);
  assert(subghz_protocol_registry_selftest(NULL, 0));
  char tiny[2] = {'x', 'x'};
  assert(subghz_protocol_registry_selftest(tiny, sizeof(tiny)));
  assert(tiny[1] == '\0');
  assert(subghz_protocol_registry_get_by_name("CAME") != NULL);
  assert(subghz_protocol_registry_get_by_name("Nice FLO") != NULL);
  assert(subghz_protocol_registry_get_by_name("RCSwitch(P1)") == NULL);
  assert(subghz_protocol_registry_get_by_name("KeeLoq") != NULL);
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

  data.protocol_name = "RCSwitch(P1)";
  data.bit_count = 33;
  assert(subghz_protocol_registry_encode(data.protocol_name, &data, pulses, TEST_PULSE_CAPACITY) == 0);
  data.bit_count = 0;
  assert(subghz_protocol_registry_encode(data.protocol_name, &data, pulses, TEST_PULSE_CAPACITY) == 0);
  data.bit_count = 24;
  data.protocol_name = "RCSwitch(P-2147483648)";
  assert(subghz_protocol_registry_encode(data.protocol_name, &data, pulses, TEST_PULSE_CAPACITY) == 0);
}

static void test_princeton_complete_frame(void) {
  const uint32_t values[] = {0xA1B2C3, 0x5AA55A, 0x13579B, 0xFFFFFF};
  const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name("Princeton");
  for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
    int32_t pulses[200];
    for (int bit = 23; bit >= 0; --bit) {
      bool one = (values[v] >> bit) & 1;
      pulses[(23 - bit) * 2] = one ? 1200 : 400;
      pulses[(23 - bit) * 2 + 1] = one ? -400 : -1200;
    }
    pulses[48] = 400;
    pulses[49] = -12000;
    for (size_t i = 1; i < 4; ++i) memcpy(pulses + i * 50, pulses, 50 * sizeof(*pulses));
    subghz_data_t out = {0};
    assert(!protocol->decode(pulses, 24, &out));
    assert(protocol->decode(pulses, 200, &out));
    assert(out.bit_count == 24 && out.raw_value == values[v]);
    assert(subghz_protocol_registry_decode_all(pulses, 200, &out));
    assert(strcmp(out.protocol_name, "Princeton") == 0);
    assert(out.bit_count == 24 && out.raw_value == values[v]);
    assert(strstr(out.protocol_fields, "Protocol: Princeton\n") != NULL);
    flipper_subghz_result_t binary;
    assert(flipper_subghz_decode_named("BinRAW", pulses, 200, &binary) == ESP_OK);
    assert(strcmp(binary.name, "BinRAW") == 0);
    assert(strstr(binary.fields, "Data_RAW:") != NULL);
  }
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

static void test_large_raw_replay(void) {
  static int32_t pulses[2395];
  for (size_t i = 0; i < 2395; ++i) pulses[i] = (i % 2) ? -50000 : 350;
  size_t length = subghz_protocol_serialize_raw(pulses, 2395, 868350000,
                                                s_capture, sizeof(s_capture));
  assert(length > 8192 && length < sizeof(s_capture));
  assert(replay_fixture("large_raw") == ESP_OK);
  assert(s_frequency == 868350000 && s_sent_preset == TEST_PRESET);
  assert(s_sent_count == 2395 && memcmp(s_sent, pulses, sizeof(pulses)) == 0);
}

static void test_serialization_replay(void) {
  subghz_data_t data = {.protocol_name = "Nice FLO", .bit_count = 12,
                       .serial = 0xabc, .raw_value = 0xabc, .btn = TEST_BUTTON};
  size_t size = subghz_protocol_serialize_decoded(&data, TEST_FREQUENCY_HZ, 700,
                                                s_capture, sizeof(s_capture));
  assert(size == strlen(s_capture));
  assert(strstr(s_capture, "Serial: 2748\nBtn: 2\n") != NULL);
  char tiny[2] = {'x', 'x'};
  assert(subghz_protocol_serialize_decoded(&data, TEST_FREQUENCY_HZ, 700, tiny, sizeof(tiny)) == 0);
  assert(tiny[0] == '\0');
  s_is_running = true;
  assert(replay_fixture("fixture") == ESP_OK);
  assert(!s_is_running && s_sent_count != 0 && s_frequency == TEST_FREQUENCY_HZ && s_sent_preset == TEST_PRESET);
  subghz_data_t decoded = {0};
  const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name(data.protocol_name);
  assert(protocol->decode(s_sent, s_sent_count, &decoded));
  assert(decoded.raw_value == data.raw_value);
  int32_t expected_frame[TEST_PULSE_CAPACITY];
  size_t frame_count = subghz_protocol_registry_encode(data.protocol_name, &data,
                                                       expected_frame, TEST_PULSE_CAPACITY);
  assert(frame_count != 0 && s_sent_count == frame_count * TEST_NATIVE_REPEATS);
  for (size_t repeat = 0; repeat < TEST_NATIVE_REPEATS; ++repeat)
    assert(memcmp(s_sent + repeat * frame_count, expected_frame,
                  frame_count * sizeof(*expected_frame)) == 0);

  /* Legacy fixed-code captures have no Serial/Btn fields. */
  snprintf(s_capture, sizeof(s_capture),
           "Frequency: %u\nProtocol: CAME\nBit: 12\nKey: 00 00 00 00 00 00 0A BC\n",
           TEST_FREQUENCY_HZ);
  assert(replay_fixture("legacy") == ESP_OK);
  protocol = subghz_protocol_registry_get_by_name("CAME");
  assert(protocol->decode(s_sent, s_sent_count, &decoded) && decoded.raw_value == 0xabc);

  const int32_t raw[] = {350, -700, 700, -350};
  size = subghz_protocol_serialize_raw(raw, 4, TEST_FREQUENCY_HZ, s_capture, sizeof(s_capture));
  assert(size == strlen(s_capture));
  assert(replay_fixture("raw") == ESP_OK);
  assert(s_sent_count == 4 && memcmp(s_sent, raw, sizeof(raw)) == 0);
  snprintf(s_capture, sizeof(s_capture), "Protocol: Unknown\nBit: 12\n");
  s_sent_count = 0;
  assert(replay_fixture("unknown") == ESP_ERR_NOT_SUPPORTED);
  assert(s_sent_count == 0);
  snprintf(s_capture, sizeof(s_capture),
           "Protocol: CAME 12345678901234567890123456789012345678901234567890\nBit: 12\n");
  assert(replay_fixture("overlong") == ESP_ERR_INVALID_ARG);
  assert(s_sent_count == 0);
}

static void test_raw_validation(void) {
  int32_t pulses[4];uint32_t frequency=7;uint8_t preset=8;
  const char *bad[]={"1 2 3 4 5", "350,-700", "350junk", "0", "2147483648",
    "-2147483649", "-2147483648", "99999999999999999999999999999", "350 -700\nRAW_Data:"};
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
    assert(replay_fixture("invalid")!=ESP_OK);
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

}

static void test_custom_radio_replay(void) {
  const int32_t raw[] = {350, -700, 700, -350};
  assert(cc1101_config_from_preset(CC1101_PRESET_2FSK_47KHZ, &s_custom_radio) == ESP_OK);
  assert(cc1101_config_step(&s_custom_radio, CC1101_CONFIG_LNA_GAIN, 1) == ESP_OK);
  assert(cc1101_config_step(&s_custom_radio, CC1101_CONFIG_DEVIATION, 1) == ESP_OK);
  assert(cc1101_config_step(&s_custom_radio, CC1101_CONFIG_DATA_RATE, 1) == ESP_OK);
  s_has_custom_radio = true;
  for (unsigned decoded = 0; decoded < 2; ++decoded) {
    if (decoded) {
      subghz_data_t data = {.protocol_name = "Nice FLO", .bit_count = 12,
                           .serial = 0xabc, .raw_value = 0xabc, .btn = TEST_BUTTON};
      assert(subghz_protocol_serialize_decoded(&data, TEST_FREQUENCY_HZ, 700,
                                             s_capture, sizeof(s_capture)) > 0);
    } else {
      assert(subghz_protocol_serialize_raw(raw, 4, TEST_FREQUENCY_HZ,
                                         s_capture, sizeof(s_capture)) > 0);
    }
    assert(strstr(s_capture, "Radio:") != NULL);
    assert(replay_fixture("custom") == ESP_OK);
    assert(s_sent_radio.mdmcfg4 == s_custom_radio.mdmcfg4 &&
           s_sent_radio.mdmcfg3 == s_custom_radio.mdmcfg3 &&
           s_sent_radio.deviatn == s_custom_radio.deviatn &&
           s_sent_radio.agcctrl2 == s_custom_radio.agcctrl2);
  }
  const char *bad[] = {"Radio: 103 131 71", "Radio: 103 131 71 7 garbage",
      "Radio: 103 131 199 7", "Radio: 256 131 71 7", "Radio: -1 131 71 7",
      "Radio: 103 131 71 7\nRadio: 103 131 71 7", "Radio: 0 0 0 0"};
  for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
    snprintf(s_capture, sizeof(s_capture),
        "Frequency: 433920000\nPreset: 4\n%s\nProtocol: RAW\nRAW_Data: 350 -700\n", bad[i]);
    size_t before = s_sent_count;
    assert(replay_fixture("bad_custom") == ESP_ERR_INVALID_ARG);
    assert(s_sent_count == before);
  }
  s_has_custom_radio = false;
}

static void test_flipper_raw_files(void) {
  /* Flipper .sub format permits repeated RAW_Data fields and CRLF line endings. */
  const char *names[] = {"FuriHalSubGhzPresetOok270Async", "FuriHalSubGhzPresetOok650Async",
                        "FuriHalSubGhzPreset2FSKDev238Async", "FuriHalSubGhzPreset2FSKDev476Async"};
  const int32_t expected[] = {400, -1200, 1200, -400, 400, -14000};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    snprintf(s_capture, sizeof(s_capture),
             "Filetype: Flipper SubGhz RAW File\r\nVersion: 1\r\nFrequency: 433920000\r\n"
             "Preset: %s\r\nProtocol: RAW\r\nRepeat: 50\r\n"
             "RAW_Data: 400 -1200 1200\r\n# continuation\r\nRAW_Data: -400 400 -14000\r\n",
             names[i]);
    assert(replay_fixture("flipper_raw") == ESP_OK);
    assert(s_sent_preset == (cc1101_preset_t)(i + 1) && s_sent_count == 6);
    assert(memcmp(s_sent, expected, sizeof(expected)) == 0);
    int32_t too_small[5];
    uint32_t frequency = 7;
    uint8_t preset = 8;
    assert(subghz_protocol_parse_raw(s_capture, too_small, 5, &frequency, &preset) == 0);
    assert(frequency == 7 && preset == 8);
  }
  const char *bad[] = {"FuriHalSubGhzPresetCustom", "FuriHalSubGhzPreset2FSKDev12KAsync",
                       "FuriHalSubGhzPresetOok270AsyncX", "0", "7"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    snprintf(s_capture, sizeof(s_capture),
             "Frequency: 433920000\nPreset: %s\nProtocol: RAW\nRAW_Data: 400 -1200\n", bad[i]);
    s_sent_count = 0;
    assert(replay_fixture("unsupported") != ESP_OK && s_sent_count == 0);
  }
  snprintf(s_capture, sizeof(s_capture),
           "Frequency: 433920000\nPreset: 1\nRAW_Data: 400 -1200\nRAW_Data: -0\n");
  s_sent_count = 0;
  assert(replay_fixture("bad_continuation") != ESP_OK && s_sent_count == 0);
}

static void test_flipper_princeton_timing(void) {
  /* Independent vector from Flipper's MSB-first TE/3TE, stop + guard contract. */
  const uint32_t code = 0x95D5D4;
  int32_t expected[50];
  for (int bit = 23; bit >= 0; --bit) {
    expected[2 * (23 - bit)] = (code & (1U << bit)) ? 1200 : 400;
    expected[2 * (23 - bit) + 1] = (code & (1U << bit)) ? -400 : -1200;
  }
  expected[48] = 400;
  expected[49] = -14000;
  snprintf(s_capture, sizeof(s_capture),
           "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: 433920000\n"
           "Preset: FuriHalSubGhzPresetOok270Async\nProtocol: Princeton\nBit: 24\n"
           "Key: 00 00 00 00 00 95 D5 D4\nTE: 400\nGuard_time: 35\n");
  size_t header_length = strlen(s_capture);
  assert(replay_fixture("flipper_princeton") == ESP_OK);
  assert(s_sent_preset == 1 && s_sent_count == 500);
  for (size_t repeat = 0; repeat < 10; ++repeat)
    assert(memcmp(s_sent + repeat * 50, expected, sizeof(expected)) == 0);
  subghz_data_t decoded = {0};
  const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name("Princeton");
  assert(protocol->decode(s_sent, s_sent_count, &decoded));
  assert(decoded.raw_value == code && decoded.te_us == 400 && decoded.guard_time == 35);
  const char *bad_repeat[] = {"0", "101", "-1", "4294967296", "2junk", "2\nRepeat: 3"};
  for (size_t i = 0; i < sizeof(bad_repeat) / sizeof(bad_repeat[0]); ++i) {
    snprintf(s_capture + header_length, sizeof(s_capture) - header_length,
             "Repeat: %s\n", bad_repeat[i]);
    s_sent_count = 0;
    s_is_running = true;
    assert(replay_fixture("invalid_repeat") != ESP_OK);
    assert(s_sent_count == 0 && s_is_running);
  }
  snprintf(s_capture + header_length, sizeof(s_capture) - header_length, "Repeat: 3\n");
  assert(replay_fixture("repeat_three") == ESP_OK && s_sent_count == 150);
  snprintf(s_capture + header_length, sizeof(s_capture) - header_length, "Repeat: 100\n");
  assert(replay_fixture("repeat_bounded") == ESP_OK && s_sent_count == 5000);
  assert(subghz_protocol_serialize_decoded(&decoded, TEST_FREQUENCY_HZ, 350,
                                         s_capture, sizeof(s_capture)) != 0);
  assert(strstr(s_capture, "TE: 400\nGuard_time: 35\n") != NULL);
  assert(replay_fixture("measured_princeton") == ESP_OK);
  assert(memcmp(s_sent, expected, sizeof(expected)) == 0);

  const char *invalid[] = {"TE: 0", "TE: -1", "TE: 4294967295", "TE: 400x",
                          "TE: 400\nTE: 500", "TE: 400\nGuard_time: -1"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    snprintf(s_capture, sizeof(s_capture),
             "Frequency: 433920000\nPreset: 1\nProtocol: Princeton\nBit: 24\n"
             "Key: 00 00 00 00 00 95 D5 D4\n%s\n", invalid[i]);
    s_sent_count = 0;
    s_is_running = true;
    assert(replay_fixture("bad_timing") != ESP_OK);
    assert(s_sent_count == 0 && s_is_running);
  }
}



int subghz_test_disk_read(const char *name, char *out, size_t capacity);
static void test_capture_storage(void) {
  static int32_t pulses[TEST_PULSE_CAPACITY], recovered[TEST_PULSE_CAPACITY];
  for (size_t i = 0; i < TEST_PULSE_CAPACITY; ++i)
    pulses[i] = i % 2 ? -30000 : 30000;
  assert(subghz_storage_init() == ESP_OK);
  assert(subghz_storage_save_raw("full", pulses, TEST_PULSE_CAPACITY, TEST_FREQUENCY_HZ) == ESP_OK);
  assert(subghz_storage_save_raw("full", pulses, 2, TEST_FREQUENCY_HZ) != ESP_OK);
  assert(subghz_test_disk_read("full", s_capture, sizeof(s_capture)) > 0);
  uint32_t frequency; uint8_t preset;
  assert(subghz_protocol_parse_raw(s_capture, recovered, TEST_PULSE_CAPACITY, &frequency, &preset) == TEST_PULSE_CAPACITY);
  assert(memcmp(pulses, recovered, sizeof(pulses)) == 0);
  assert(subghz_storage_save_raw("", pulses, 2, TEST_FREQUENCY_HZ) == ESP_ERR_INVALID_ARG);
  assert(subghz_storage_save_raw("new", pulses, 2, TEST_FREQUENCY_HZ) == ESP_OK);
  assert(subghz_storage_save_raw("middle", pulses, 2, TEST_FREQUENCY_HZ) == ESP_OK);
  struct utimbuf times = {.actime = 100, .modtime = 100};
  assert(utime(TOS_PATH_SUBGHZ "/full.sub", &times) == 0);
  times.modtime = 200; assert(utime(TOS_PATH_SUBGHZ "/middle.sub", &times) == 0);
  times.modtime = 300; assert(utime(TOS_PATH_SUBGHZ "/new.sub", &times) == 0);
  subghz_storage_entry_t files[2];
  assert(subghz_storage_list(files, 2) == 2);
  assert(strcmp(files[0].name, "new") == 0 && strcmp(files[1].name, "middle") == 0);
  assert(subghz_storage_save_capture("manual", s_capture) == ESP_OK);
  assert(subghz_storage_save_capture("manual", "changed") == ESP_ERR_INVALID_STATE);
  assert(subghz_storage_save_capture("../escape", s_capture) == ESP_ERR_INVALID_ARG);
  assert(subghz_storage_save_raw("../escape", pulses, 2, TEST_FREQUENCY_HZ) == ESP_ERR_INVALID_ARG);
  assert(subghz_storage_delete("../escape") == ESP_ERR_INVALID_ARG);
  char saved[64];
  assert(subghz_test_disk_read("../escape", saved, sizeof(saved)) == -1);
  assert(subghz_test_disk_read("manual", saved, sizeof(saved)) > 0);
  assert(strncmp(saved, s_capture, strlen(saved)) == 0);
  puts("PASS: real storage full 8192-pulse RAW persistence, exclusive saves and bounded newest-first listing");
}

static void test_external_raw(const char *path) {
  FILE *file = fopen(path, "rb");
  assert(file != NULL);
  size_t length = fread(s_capture, 1, sizeof(s_capture) - 1, file);
  assert(length > 0 && length < sizeof(s_capture) - 1 && !ferror(file));
  assert(fclose(file) == 0 && memchr(s_capture, '\0', length) == NULL);
  s_capture[length] = '\0';
  assert(replay_fixture("external_raw") == ESP_OK);
  printf("CAPTURE_METADATA: %" PRIu32 " %u\n", s_frequency, (unsigned)s_sent_preset);
  printf("CAPTURE_PULSES:");
  for (size_t i = 0; i < s_sent_count; ++i)
    printf(" %" PRId32, s_sent[i]);
  puts("");
}

void test_flipper_platform(const char *path);
int main(int argc, char **argv) {
  test_flipper_platform(TOS_PATH_SUBGHZ "_keystore.txt");
  test_capture_storage();
  test_stream_file();
  test_catalog();
  test_registry();
  test_princeton_complete_frame();
  test_pwm_vector();
  test_serialization_replay();
  test_custom_radio_replay();
  test_large_raw_replay();
  test_raw_validation();
  test_decoded_validation();
  test_keeloq_vectors();
  test_flipper_raw_files();
  test_flipper_princeton_timing();
  puts("PASS: Princeton round-trips, exact-name lookup/bounds, fixed PWM vector, serialization/replay,");
  puts("      legacy cipher/frame vectors (no physical radio).");
  puts("PASS: Flipper named presets, multiline RAW, independent Princeton TE/guard vector,");
  puts("      bounded repeats, malformed files and measured Princeton timing retention.");
  if (argc == 2) test_external_raw(argv[1]);
  return 0;
}
