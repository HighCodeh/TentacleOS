#ifndef HLE_ESP_ROM_CRC_H
#define HLE_ESP_ROM_CRC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches ESP-IDF's complemented, chainable reflected CRC-16 ROM API. */
uint16_t esp_rom_crc16_le(uint16_t crc, const uint8_t *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
