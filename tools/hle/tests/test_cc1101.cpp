#include <gtest/gtest.h>
#include "hle/cc1101_emu.h"

using namespace hle;

TEST(CC1101Emulator, ResetState) {
    CC1101Emulator cc;
    EXPECT_EQ(cc.state(), CC_IDLE);
    EXPECT_EQ(cc.chip_status(), 0x0F);
    EXPECT_FALSE(cc.gdo0());
    EXPECT_FALSE(cc.gdo2());
}

TEST(CC1101Emulator, ReadWriteRegisters) {
    CC1101Emulator cc;
    cc.write_register(0x00, 0xAA);
    EXPECT_EQ(cc.read_register(0x00), 0xAA);

    cc.write_register(0x2E, 0x55);
    EXPECT_EQ(cc.read_register(0x2E), 0x55);
}

TEST(CC1101Emulator, StrobeTransitions) {
    CC1101Emulator cc;
    EXPECT_EQ(cc.state(), CC_IDLE);

    cc.strobe(0x34); // STX → TX
    EXPECT_EQ(cc.state(), CC_TX);
    EXPECT_EQ(cc.chip_status(), 0x02);

    cc.strobe(0x30); // SIDLE → IDLE
    EXPECT_EQ(cc.state(), CC_IDLE);

    cc.strobe(0x36); // SRX → RX
    EXPECT_EQ(cc.state(), CC_RX);
    EXPECT_EQ(cc.chip_status(), 0x01);
}

TEST(CC1101Emulator, FrequencySetting) {
    CC1101Emulator cc;
    cc.set_frequency_mhz(433.92f);
    float f = cc.frequency_mhz();
    EXPECT_NEAR(f, 433.92f, 0.1f);
}

TEST(CC1101Emulator, FIFOOperations) {
    CC1101Emulator cc;

    // Write to TX FIFO
    cc.write_register(0x3F, 0x11);
    cc.write_register(0x3F, 0x22);
    cc.write_register(0x3F, 0x33);

    // Read from TX FIFO
    EXPECT_EQ(cc.read_register(0x3F), 0x11);
    EXPECT_EQ(cc.read_register(0x3F), 0x22);
    EXPECT_EQ(cc.read_register(0x3F), 0x33);

    // RX FIFO injection
    uint8_t rx_data[] = {0xAA, 0xBB};
    cc.inject_rx_data(rx_data, 2);
    // Not directly readable via read_register without burst
}

TEST(CC1101Emulator, PATABLE) {
    CC1101Emulator cc;
    cc.write_register(0x3E, 0xC0);
    EXPECT_EQ(cc.read_register(0x3E), 0xC0);
}

TEST(CC1101Emulator, GDOControl) {
    CC1101Emulator cc;
    cc.set_gdo0(true);
    EXPECT_TRUE(cc.gdo0());
    cc.set_gdo2(true);
    EXPECT_TRUE(cc.gdo2());
    cc.set_gdo0(false);
    EXPECT_FALSE(cc.gdo0());
}
