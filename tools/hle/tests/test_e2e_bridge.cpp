#include <gtest/gtest.h>
#include <thread>
#include <cstring>
#include "hle/spi_bridge_channel.h"
#include "hle/cc1101_emu.h"

using namespace hle;

extern "C" {
#include "esp_err.h"
void hle_set_bridge_channel(hle::SPIBridgeChannel *ch);
}

static constexpr uint16_t SYSTEM_PING_COMMAND = 0x0001;
static constexpr uint16_t SYSTEM_STATUS_COMMAND = 0x0002;
static constexpr uint16_t WIFI_SCAN_COMMAND = 0x0120;
static constexpr uint16_t WIFI_STREAM_COMMAND = 0x0125;

TEST(E2EBridge, SystemPingViaBridge) {
    SPIBridgeChannel ch;
    hle_set_bridge_channel(&ch);

    // Emulate C5 side (normally runs in bridge_task)
    std::thread c5_side([&ch]() {
        uint16_t cmd_id;
        uint8_t payload[256], payload_len;
        ASSERT_TRUE(ch.slave_wait_command(cmd_id, payload, payload_len));

        // Handle SYSTEM_PING
        if (cmd_id == SYSTEM_PING_COMMAND) {
            ch.slave_send_response(cmd_id, 0x00, nullptr, 0);
            ch.slave_notify_irq();
        }
    });

    // P4 side: send ping command
    ch.master_send_command(SYSTEM_PING_COMMAND, nullptr, 0);
    ASSERT_TRUE(ch.master_wait_irq(1000));

    uint16_t resp_id;
    uint8_t resp_payload[256], resp_len;
    ASSERT_TRUE(ch.master_receive_response(resp_id, resp_payload, resp_len, 100));
    EXPECT_EQ(resp_id, SYSTEM_PING_COMMAND);
    EXPECT_GE(resp_len, 1u);
    EXPECT_EQ(resp_payload[0], 0x00); // OK

    c5_side.join();
}

TEST(E2EBridge, SystemStatus) {
    SPIBridgeChannel ch;
    hle_set_bridge_channel(&ch);

    std::thread c5_side([&ch]() {
        uint16_t cmd_id;
        uint8_t payload[256], payload_len;
        ASSERT_TRUE(ch.slave_wait_command(cmd_id, payload, payload_len));

        // Emulate system status response with mock data
        if (cmd_id == SYSTEM_STATUS_COMMAND) {
            // STATUS response: [status_byte, wifi_active, bt_running, firmware_ver_major, minor, patch]
            uint8_t status_data[] = {0x01, 0x00, 0x01, 0x00, 0x05};  // wifi inactive, bt running, v1.0.5
            ch.slave_send_response(cmd_id, 0x00, status_data, sizeof(status_data));
            ch.slave_notify_irq();
        }
    });

    ch.master_send_command(SYSTEM_STATUS_COMMAND, nullptr, 0);
    ASSERT_TRUE(ch.master_wait_irq(1000));

    uint16_t resp_id;
    uint8_t resp_payload[256], resp_len;
    ASSERT_TRUE(ch.master_receive_response(resp_id, resp_payload, resp_len, 100));
    EXPECT_EQ(resp_id, SYSTEM_STATUS_COMMAND);
    EXPECT_EQ(resp_payload[0], 0x00); // OK
    // status_data follows at offset 1
    EXPECT_EQ(resp_payload[1], 0x01); // wifi_active
    EXPECT_EQ(resp_payload[2], 0x00); // bt_running

    c5_side.join();
}

TEST(E2EBridge, WiFiScanDataFetch) {
    SPIBridgeChannel ch;
    hle_set_bridge_channel(&ch);

    const int ap_count = 3;

    std::thread c5_side([&ch, ap_count]() {
        uint16_t cmd_id;
        uint8_t payload[256], payload_len;
        ASSERT_TRUE(ch.slave_wait_command(cmd_id, payload, payload_len));

        // Respond with count
        if (cmd_id == WIFI_SCAN_COMMAND) {
            uint8_t count[2] = {uint8_t(ap_count), 0};
            ch.slave_send_response(cmd_id, 0x00, count, 2);
            ch.slave_notify_irq();
        }
    });

    ch.master_send_command(WIFI_SCAN_COMMAND, nullptr, 0);
    ASSERT_TRUE(ch.master_wait_irq(1000));

    uint16_t resp_id;
    uint8_t resp_payload[256], resp_len;
    ASSERT_TRUE(ch.master_receive_response(resp_id, resp_payload, resp_len, 200));
    EXPECT_EQ(resp_id, WIFI_SCAN_COMMAND);
    EXPECT_EQ(resp_payload[1], (uint8_t)ap_count);

    c5_side.join();
}

TEST(E2EBridge, StreamBackpressure) {
    SPIBridgeChannel ch;
    hle_set_bridge_channel(&ch);

    uint8_t data[64] = {};
    for (int i = 0; i < 64; i++) data[i] = (uint8_t)i;

    // Fill stream queue to capacity (8)
    for (int i = 0; i < 8; i++) {
        ASSERT_TRUE(ch.stream_push(WIFI_STREAM_COMMAND, data, 64));
    }
    // 9th should fail
    EXPECT_FALSE(ch.stream_push(WIFI_STREAM_COMMAND, data, 64));

    // Drain one
    uint16_t out_id;
    uint8_t out_data[256];
    size_t out_len;
    ASSERT_TRUE(ch.stream_pop(out_id, out_data, out_len));

    // Now should accept one more
    EXPECT_TRUE(ch.stream_push(WIFI_STREAM_COMMAND, data, 64));
}
