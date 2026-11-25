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
#include "subghz_transmitter.h"
#include "subghz_storage.h"
#include "tos_storage_paths.h"
#include <utime.h>
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

static void test_princeton_complete_frame(void) {
  const uint32_t values[] = {0xA1B2C3, 0x5AA55A, 0x13579B, 0, 0xFFFFFF};
  const subghz_protocol_t *princeton = subghz_protocol_registry_get_by_name("Princeton");
  const subghz_protocol_t *nice = subghz_protocol_registry_get_by_name("Nice Flo");
  for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
    int32_t pulses[50];
    for (int bit = 23; bit >= 0; --bit) {
      bool one = (values[v] >> bit) & 1;
      pulses[(23 - bit) * 2] = one ? 1200 : 400;
      pulses[(23 - bit) * 2 + 1] = one ? -400 : -1200;
    }
    pulses[48] = 400;
    pulses[49] = -12000;
    subghz_data_t out = {0};
    assert(!nice->decode(pulses, 50, &out));
    assert(princeton->decode(pulses, 50, &out));
    assert(out.bit_count == 24 && out.raw_value == values[v]);
    assert(subghz_protocol_registry_decode_all(pulses, 50, &out));
    assert(out.bit_count == 24 && out.raw_value == values[v]);
    assert(!princeton->decode(pulses, 24, &out));
    assert(!princeton->decode(pulses, 48, &out));
    assert(princeton->decode(pulses, 49, &out));
    pulses[49] = -1200;
    assert(!princeton->decode(pulses, 50, &out));
    pulses[49] = -12000;
    pulses[17] = -4000;
    assert(!princeton->decode(pulses, 50, &out));
  }
  subghz_data_t data = {.raw_value = 0xA1B2C3, .bit_count = 24};
  int32_t encoded[50];
  assert(princeton->encode(&data, encoded, 50) == 50);
  assert(encoded[48] == 350 && encoded[49] == -10500);
  assert(princeton->encode(&data, encoded, 49) == 0);
}

static void test_rcswitch_frame_boundaries(void) {
  const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name("RCSwitch");
  const uint32_t code = 0x13579B;
  int32_t pulses[104] = {400, -12400};
  for (int bit = 23; bit >= 0; --bit) {
    bool one = (code >> bit) & 1;
    pulses[2 + (23 - bit) * 2] = one ? 1200 : 400;
    pulses[3 + (23 - bit) * 2] = one ? -400 : -1200;
  }
  pulses[50] = 400;
  pulses[51] = -12400;
  subghz_data_t out = {0};
  assert(protocol->decode(pulses, 52, &out));
  assert(out.bit_count == 24 && out.raw_value == code);
  assert(!protocol->decode(pulses, 36, &out));
  assert(!protocol->decode(pulses, 50, &out));
  assert(!protocol->decode(NULL, 52, &out));
  assert(!protocol->decode(pulses, 52, NULL));
  memcpy(pulses + 52, pulses, 52 * sizeof(pulses[0]));
  pulses[36] = 5000;
  assert(!protocol->decode(pulses, 52, &out));
  assert(protocol->decode(pulses, 104, &out));
  assert(out.bit_count == 24 && out.raw_value == code);

  /* Variable-length protocols remain valid when their frame is complete. */
  const uint8_t lengths[] = {12, 17, 24, 32};
  for (size_t i = 0; i < sizeof(lengths); ++i) {
    uint8_t bits = lengths[i];
    uint32_t value = 0x13579B & (UINT32_MAX >> (32 - bits));
    size_t end = 2 + bits * 2;
    pulses[0] = 400;
    pulses[1] = -12400;
    for (int bit = bits - 1; bit >= 0; --bit) {
      bool one = (value >> bit) & 1;
      pulses[2 + (bits - 1 - bit) * 2] = one ? 1200 : 400;
      pulses[3 + (bits - 1 - bit) * 2] = one ? -400 : -1200;
    }
    pulses[end] = 400;
    pulses[end + 1] = -12400;
    assert(protocol->decode(pulses, end + 2, &out));
    assert(out.bit_count == bits && out.raw_value == value);
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
  assert(subghz_replay_file("large_raw") == ESP_OK);
  assert(s_frequency == 868350000 && s_sent_preset == TEST_PRESET);
  assert(s_sent_count == 2395 && memcmp(s_sent, pulses, sizeof(pulses)) == 0);
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

static void test_custom_radio_replay(void) {
  const int32_t raw[] = {350, -700, 700, -350};
  assert(cc1101_config_from_preset(CC1101_PRESET_2FSK_47KHZ, &s_custom_radio) == ESP_OK);
  assert(cc1101_config_step(&s_custom_radio, CC1101_CONFIG_LNA_GAIN, 1) == ESP_OK);
  assert(cc1101_config_step(&s_custom_radio, CC1101_CONFIG_DEVIATION, 1) == ESP_OK);
  assert(cc1101_config_step(&s_custom_radio, CC1101_CONFIG_DATA_RATE, 1) == ESP_OK);
  s_has_custom_radio = true;
  for (unsigned decoded = 0; decoded < 2; ++decoded) {
    if (decoded) {
      subghz_data_t data = {.protocol_name = "Nice Flo 12bit", .bit_count = 12,
                           .serial = 0xabc, .raw_value = 0xabc, .btn = TEST_BUTTON};
      assert(subghz_protocol_serialize_decoded(&data, TEST_FREQUENCY_HZ, 700,
                                             s_capture, sizeof(s_capture)) > 0);
    } else {
      assert(subghz_protocol_serialize_raw(raw, 4, TEST_FREQUENCY_HZ,
                                         s_capture, sizeof(s_capture)) > 0);
    }
    assert(strstr(s_capture, "Radio:") != NULL);
    assert(subghz_replay_file("custom") == ESP_OK);
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
    assert(subghz_replay_file("bad_custom") == ESP_ERR_INVALID_ARG);
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
    assert(subghz_replay_file("flipper_raw") == ESP_OK);
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
    assert(subghz_replay_file("unsupported") != ESP_OK && s_sent_count == 0);
  }
  snprintf(s_capture, sizeof(s_capture),
           "Frequency: 433920000\nPreset: 1\nRAW_Data: 400 -1200\nRAW_Data: -0\n");
  s_sent_count = 0;
  assert(subghz_replay_file("bad_continuation") != ESP_OK && s_sent_count == 0);
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
  assert(subghz_replay_file("flipper_princeton") == ESP_OK);
  assert(s_sent_preset == 1 && s_sent_count == 500);
  for (size_t repeat = 0; repeat < 10; ++repeat)
    assert(memcmp(s_sent + repeat * 50, expected, sizeof(expected)) == 0);
  subghz_data_t decoded = {0};
  const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name("Princeton");
  assert(protocol->decode(expected, 50, &decoded));
  assert(decoded.raw_value == code && decoded.te_us == 400 && decoded.guard_time == 35);
  const char *bad_repeat[] = {"0", "101", "-1", "4294967296", "2junk", "2\nRepeat: 3"};
  for (size_t i = 0; i < sizeof(bad_repeat) / sizeof(bad_repeat[0]); ++i) {
    snprintf(s_capture + header_length, sizeof(s_capture) - header_length,
             "Repeat: %s\n", bad_repeat[i]);
    s_sent_count = 0;
    s_is_running = true;
    assert(subghz_replay_file("invalid_repeat") != ESP_OK);
    assert(s_sent_count == 0 && s_is_running);
  }
  snprintf(s_capture + header_length, sizeof(s_capture) - header_length, "Repeat: 3\n");
  assert(subghz_replay_file("repeat_three") == ESP_OK && s_sent_count == 150);
  snprintf(s_capture + header_length, sizeof(s_capture) - header_length, "Repeat: 100\n");
  assert(subghz_replay_file("repeat_bounded") == ESP_OK && s_sent_count == 5000);
  assert(subghz_protocol_serialize_decoded(&decoded, TEST_FREQUENCY_HZ, 350,
                                         s_capture, sizeof(s_capture)) != 0);
  assert(strstr(s_capture, "TE: 400\nGuard_time: 35\n") != NULL);
  assert(subghz_replay_file("measured_princeton") == ESP_OK);
  assert(memcmp(s_sent, expected, sizeof(expected)) == 0);
  decoded.te_us = UINT32_MAX;
  assert(protocol->encode(&decoded, expected, 50) == 0);
  const char *invalid[] = {"TE: 0", "TE: -1", "TE: 4294967295", "TE: 400x",
                          "TE: 400\nTE: 500", "TE: 400\nGuard_time: -1"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    snprintf(s_capture, sizeof(s_capture),
             "Frequency: 433920000\nPreset: 1\nProtocol: Princeton\nBit: 24\n"
             "Key: 00 00 00 00 00 95 D5 D4\n%s\n", invalid[i]);
    s_sent_count = 0;
    s_is_running = true;
    assert(subghz_replay_file("bad_timing") != ESP_OK);
    assert(s_sent_count == 0 && s_is_running);
  }
}

static void test_rcswitch_measured_timing(void) {
  subghz_data_t data = {.protocol_name = "RCSwitch(P1)", .bit_count = 24,
                       .raw_value = 0xA1B2C3, .te_us = 420};
  const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name("RCSwitch");
  int32_t frame[52];
  assert(protocol->encode(&data, frame, 52) == 52);
  assert(frame[0] == 420 && frame[1] == -13020);
  subghz_data_t decoded = {0};
  assert(protocol->decode(frame, 52, &decoded) && decoded.te_us == 420);
  assert(subghz_protocol_serialize_decoded(&decoded, TEST_FREQUENCY_HZ, 350,
                                         s_capture, sizeof(s_capture)) != 0);
  assert(strstr(s_capture, "TE: 420\n") != NULL);
  assert(subghz_replay_file("measured_rcswitch") == ESP_OK);
  assert(s_sent_count == 52 * TEST_NATIVE_REPEATS);
  assert(memcmp(s_sent, frame, sizeof(frame)) == 0);
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
  char saved[64];
  assert(subghz_test_disk_read("manual", saved, sizeof(saved)) > 0);
  assert(strncmp(saved, s_capture, strlen(saved)) == 0);
  puts("PASS: real storage full 8192-pulse RAW persistence, exclusive saves and bounded newest-first listing");
}

int main(void) {
  test_capture_storage();
  test_registry();
  test_princeton_complete_frame();
  test_rcswitch_frame_boundaries();
  test_pwm_vector();
  test_serialization_replay();
  test_custom_radio_replay();
  test_large_raw_replay();
  test_raw_validation();
  test_decoded_validation();
  test_keeloq_vectors();
  test_flipper_raw_files();
  test_flipper_princeton_timing();
  test_rcswitch_measured_timing();
  puts("PASS: 12 protocol round-trips, lookup/bounds, fixed PWM vector, serialization/replay,");
  puts("      cipher/frame vectors and synthetic long-name KeeLoq replay (no physical radio).");
  puts("PASS: Flipper named presets, multiline RAW, independent Princeton TE/guard vector,");
  puts("      bounded repeats, malformed files and measured RCSwitch timing retention.");
  return 0;
}
