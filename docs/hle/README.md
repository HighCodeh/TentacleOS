# Host HLE compatibility

The Linux HLE target uses host-side ESP-IDF shims. It does not exercise physical
radios, microphones, DMA or flash, and its test results are not device validation.

## Protocol CRC

`tools/hle/include/esp_rom_crc.h` and `tools/hle/src/esp_rom_crc.cpp` supply only
the ROM function used by the paired SPI protocol: `esp_rom_crc16_le`. The shim
matches ESP-IDF's reflected CRC-16 polynomial (`0x8408`) and complements the
accumulator on entry and exit. Passing a returned value into the next call
therefore continues the CRC over the concatenated input. An empty input returns
the seed unchanged. This is not a stub that accepts arbitrary frames.

Reference semantics: ESP-IDF `components/esp_rom/include/esp_rom_crc.h` and
`components/esp_rom/linux/esp_rom_crc.c`, function `esp_rom_crc16_le`. Firmware
continues to use the real ESP-IDF header and implementation, without changes to
either production copy of `spi_protocol.h`.

`test_esp_rom_crc.cpp` checks the standard `123456789` vectors (X-25 check
`0x906e`, complemented KERMIT check `0xde76`), binary data, every empty-input
seed, split-buffer chaining, real SPI frame golden bytes and single-bit
corruption rejection. The binary and frame goldens were independently calculated
using bit reversal and the non-reflected CRC-CCITT implementation in Python's
`binascii.crc_hqx`.

## Headless regression suite

On Linux, with CMake, a C/C++ compiler, Git and network access for pinned test
dependencies:

```sh
cmake -S tools/hle -B build/hle -DHLE_BUILD_INTERACTIVE=OFF
cmake --build build/hle --target hle_tests --parallel 2
ctest --test-dir build/hle --output-on-failure
```

The headless suite does not require SDL2. The full HLE executable still requires
Linux's GNU-compatible linker wrapping for filesystem shims; macOS-only CRC
checks do not substitute for running the Linux suite. See the repository README
for the interactive simulator and its additional dependencies.
