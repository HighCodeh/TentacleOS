#include <gtest/gtest.h>
#include <cstring>

#include "esp_crc.h"
#include "esp_rom_crc.h"
#include "spi_protocol.h"

static const uint8_t CHECK_INPUT[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

TEST(EspRomCrc, Crc32MatchesCheckValue) {
    EXPECT_EQ(esp_rom_crc32_le(0, CHECK_INPUT, sizeof(CHECK_INPUT)), 0xCBF43926u);
    EXPECT_EQ(esp_crc32_le(0, CHECK_INPUT, sizeof(CHECK_INPUT)), 0xCBF43926u);
}

TEST(EspRomCrc, Crc16MatchesCheckValue) {
    EXPECT_EQ(esp_rom_crc16_le(0, CHECK_INPUT, sizeof(CHECK_INPUT)), 0x906Eu);
}

TEST(EspRomCrc, ChainsAcrossCalls) {
    uint16_t split = esp_rom_crc16_le(0xFFFF, CHECK_INPUT, 4);
    split = esp_rom_crc16_le(split, CHECK_INPUT + 4, sizeof(CHECK_INPUT) - 4);
    EXPECT_EQ(split, esp_rom_crc16_le(0xFFFF, CHECK_INPUT, sizeof(CHECK_INPUT)));
}

TEST(EspRomCrc, SpiFrameSealRoundTrip) {
    uint8_t frame[SPI_FRAME_SIZE] = {};
    auto *header = reinterpret_cast<spi_header_t *>(frame);
    header->sync = SPI_SYNC_BYTE;
    header->type = SPI_TYPE_RESP;
    spi_header_set_cmd(header, 0x0102);
    header->length = 3;
    frame[sizeof(spi_header_t)] = 0x00;
    frame[sizeof(spi_header_t) + 1] = 0x42;
    frame[sizeof(spi_header_t) + 2] = 0x7F;

    EXPECT_FALSE(spi_frame_valid(header, header->length));
    spi_frame_seal(header, header->length);
    EXPECT_TRUE(spi_frame_valid(header, header->length));

    frame[sizeof(spi_header_t) + 1] ^= 0x01;
    EXPECT_FALSE(spi_frame_valid(header, header->length));
}
