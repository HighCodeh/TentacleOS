#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esp_rom_crc.h"
#include "spi_protocol.h"

static constexpr uint16_t CRC_INITIAL = 0xFFFF;
static constexpr std::array<uint8_t, 9> CHECK_TEXT = {
    '1', '2', '3', '4', '5', '6', '7', '8', '9'
};
static constexpr std::array<uint8_t, 7> BINARY_TEXT = {
    0x00, 0xFF, 0x80, 0x01, 0x7F, 0xAA, 0x55
};

TEST(EspRomCrc, KnownReflectedAndComplementedVectors) {
    // CRC-16/X-25 check, then the complemented KERMIT check required by this API.
    EXPECT_EQ(esp_rom_crc16_le(0, CHECK_TEXT.data(), CHECK_TEXT.size()), 0x906Eu);
    EXPECT_EQ(esp_rom_crc16_le(CRC_INITIAL, CHECK_TEXT.data(), CHECK_TEXT.size()), 0xDE76u);
    EXPECT_EQ(esp_rom_crc16_le(0x1234, BINARY_TEXT.data(), BINARY_TEXT.size()), 0x89D6u);

    std::array<uint8_t, 256> all_bytes{};
    for (size_t index = 0; index < all_bytes.size(); ++index) {
        all_bytes[index] = static_cast<uint8_t>(index);
    }
    EXPECT_EQ(esp_rom_crc16_le(CRC_INITIAL, all_bytes.data(), all_bytes.size()), 0x27BEu);
}

TEST(EspRomCrc, EmptyInputPreservesEverySeed) {
    for (uint32_t seed = 0; seed <= UINT16_MAX; ++seed) {
        EXPECT_EQ(esp_rom_crc16_le(static_cast<uint16_t>(seed), nullptr, 0), seed);
    }
}

TEST(EspRomCrc, ChunkingPreservesTheAccumulator) {
    const std::array<uint16_t, 4> seeds = {0, 1, 0x1234, CRC_INITIAL};
    for (uint16_t seed : seeds) {
        const uint16_t expected = esp_rom_crc16_le(seed, BINARY_TEXT.data(), BINARY_TEXT.size());
        for (size_t split = 0; split <= BINARY_TEXT.size(); ++split) {
            const uint16_t prefix = esp_rom_crc16_le(seed, BINARY_TEXT.data(), split);
            EXPECT_EQ(esp_rom_crc16_le(prefix, BINARY_TEXT.data() + split,
                                      BINARY_TEXT.size() - split), expected);
        }
    }
}

TEST(EspRomCrc, RealSpiHeaderUsesTheRomSeedAndByteOrder) {
    spi_header_t header{};
    header.sync = SPI_SYNC_BYTE;
    header.type = SPI_TYPE_CMD;
    spi_header_set_cmd(&header, SPI_ID_SYSTEM_PING);
    header.length = 0;

    EXPECT_EQ(spi_frame_crc(&header, 0), 0xFA9Cu);
    spi_frame_seal(&header, 0);
    EXPECT_TRUE(spi_frame_valid(&header, 0));
    uint16_t stored = 0;
    memcpy(&stored, &header.crc, sizeof(stored));
    EXPECT_EQ(stored, 0xFA9Cu);
}

TEST(EspRomCrc, RealSpiFrameRejectsProtectedBitCorruption) {
    struct __attribute__((packed)) test_frame_t {
        spi_header_t header;
        uint8_t data[5];
    } frame{};
    static_assert(offsetof(test_frame_t, data) == sizeof(spi_header_t));
    frame.header.sync = SPI_SYNC_BYTE;
    frame.header.type = SPI_TYPE_RESP;
    spi_header_set_cmd(&frame.header, 0x1106);
    frame.header.length = sizeof(frame.data);
    const std::array<uint8_t, 5> data = {0x00, 0x80, 0xFF, 0xAA, 0x55};
    memcpy(frame.data, data.data(), data.size());

    // Independent golden for bytes 02 11 06 05 00 80 FF AA 55, excluding sync/CRC.
    EXPECT_EQ(spi_frame_crc(&frame.header, sizeof(frame.data)), 0x4EB2u);
    spi_frame_seal(&frame.header, sizeof(frame.data));
    ASSERT_TRUE(spi_frame_valid(&frame.header, sizeof(frame.data)));
    const auto *bytes = reinterpret_cast<const uint8_t *>(&frame);
    EXPECT_EQ(bytes[offsetof(spi_header_t, crc)], 0xB2u);
    EXPECT_EQ(bytes[offsetof(spi_header_t, crc) + 1], 0x4Eu);

    auto *mutable_bytes = reinterpret_cast<uint8_t *>(&frame);
    for (size_t index = offsetof(spi_header_t, type); index < sizeof(frame); ++index) {
        for (uint8_t bit = 0; bit < 8; ++bit) {
            mutable_bytes[index] ^= static_cast<uint8_t>(1u << bit);
            EXPECT_FALSE(spi_frame_valid(&frame.header, sizeof(frame.data)));
            mutable_bytes[index] ^= static_cast<uint8_t>(1u << bit);
        }
    }
    // Sync is framing, not protected by the CRC helper; the receiver checks it separately.
    frame.header.sync ^= 1;
    EXPECT_TRUE(spi_frame_valid(&frame.header, sizeof(frame.data)));
}
