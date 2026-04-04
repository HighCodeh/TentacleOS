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


#include "uuid_lookup.h"
#include <stdio.h>

typedef struct {
  uint16_t uuid16;
  const char *name;
} uuid_entry_t;

static const uuid_entry_t uuid_table[] = {
  {0x1800, "Generic Access"},
  {0x1801, "Generic Attribute"},
  {0x180A, "Device Information"},
  {0x180D, "Heart Rate"},
  {0x180F, "Battery Service"},
  {0x1812, "Human Interface Device"},
  {0x181A, "Environmental Sensing"},
  {0x2A00, "Device Name"},
  {0x2A01, "Appearance"},
  {0x2A19, "Battery Level"},
  {0x2A29, "Manufacturer Name"},
  {0x2A37, "Heart Rate Measurement"},
  {0x2A4D, "Report"},
};

const char* uuid_get_name(const ble_uuid_t *uuid) {
  if (uuid->type == BLE_UUID_TYPE_16) {
    uint16_t u16 = BLE_UUID16(uuid)->value;
    for (int i = 0; i < sizeof(uuid_table)/sizeof(uuid_entry_t); i++) {
      if (uuid_table[i].uuid16 == u16) return uuid_table[i].name;
    }
  }
  return "Unknown Service/Char";
}
