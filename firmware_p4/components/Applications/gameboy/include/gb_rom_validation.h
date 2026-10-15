/* Pure header validation for the bundled DMG core. No allocation or I/O. */
#ifndef HIGHBOY_GB_ROM_VALIDATION_H
#define HIGHBOY_GB_ROM_VALIDATION_H

#include <stddef.h>
#include <stdint.h>

#define HIGHBOY_GB_HEADER_BYTES 0x150u

/* NULL means structurally supported, not that gameplay has been verified. */
static inline const char *
highboy_gb_header_error(const uint8_t *header, size_t header_bytes, size_t file_bytes) {
  if (!header || header_bytes < HIGHBOY_GB_HEADER_BYTES)
    return "ROM header is truncated";
  if ((header[0x143] & 0xc0u) == 0xc0u)
    return "Game Boy Color only ROM is not supported";

  uint8_t checksum = 0;
  for (size_t i = 0x134; i <= 0x14c; ++i)
    checksum = (uint8_t)(checksum - header[i] - 1u);
  if (checksum != header[0x14d])
    return "ROM header checksum is invalid";

  /* Bounds match the actual num_rom_banks_mask and num_ram_banks tables. */
  if (header[0x148] > 8u || header[0x149] > 5u)
    return "ROM or RAM bank size is unsupported";
  size_t expected_bytes = (size_t)32768u << header[0x148];
  if (file_bytes != expected_bytes)
    return "ROM file size does not match its header";

  switch (header[0x147]) {
    case 0x00:
    case 0x08:
    case 0x09: /* Unbanked ROM, with optional RAM. */
      if (header[0x148] != 0 || header[0x149] > 2)
        return "Unbanked cartridge size is unsupported";
      break;
    case 0x01:
    case 0x02:
    case 0x03: /* MBC1 */
      if (header[0x148] > 6 || header[0x149] > 3)
        return "MBC1 cartridge size is unsupported";
      break;
    case 0x05:
    case 0x06: /* MBC2 includes its own 512 half-byte RAM. */
      if (header[0x148] > 3 || header[0x149] != 0)
        return "MBC2 cartridge size is unsupported";
      break;
    case 0x0f:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13: /* MBC3 */
      if (header[0x148] > 7)
        return "MBC3 cartridge size is unsupported";
      break;
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c: /* MBC5 */
      break;
    case 0x1d:
    case 0x1e:
      /* The bundled core's cart_ram table does not implement these saves. */
      return "MBC5 rumble RAM cartridge is unsupported";
    default:
      return "Cartridge controller is unsupported";
  }
  return NULL;
}

#endif
