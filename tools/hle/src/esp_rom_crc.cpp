#include "esp_rom_crc.h"

static constexpr uint16_t CRC16_REFLECTED_POLYNOMIAL = 0x8408;
static constexpr uint32_t CRC_BITS_PER_BYTE = 8;

uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *buf, uint32_t len) {
    // The ROM API complements both ends, including when called on another chunk.
    crc = static_cast<uint16_t>(~crc);
    for (uint32_t index = 0; index < len; ++index) {
        crc ^= buf[index];
        for (uint32_t bit = 0; bit < CRC_BITS_PER_BYTE; ++bit) {
            crc = static_cast<uint16_t>((crc >> 1) ^
                ((crc & 1u) ? CRC16_REFLECTED_POLYNOMIAL : 0u));
        }
    }
    return static_cast<uint16_t>(~crc);
}
