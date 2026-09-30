#pragma once

#include "esp_rom_crc.h"

static inline uint32_t esp_crc32_le(uint32_t crc, uint8_t const *buf, uint32_t len) {
  return esp_rom_crc32_le(crc, buf, len);
}
