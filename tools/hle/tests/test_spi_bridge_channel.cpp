#include <gtest/gtest.h>
#include <future>
#include <thread>
#include "hle/spi_bridge_channel.h"

using namespace hle;

static constexpr uint16_t SYSTEM_PING_COMMAND = 0x0001;
static constexpr uint16_t TEST_COMMAND = 0x0210;
static constexpr uint16_t TEST_STREAM = 0x0525;

TEST(SPIBridgeChannel, PingPong) {
    SPIBridgeChannel ch;

    // Slave thread: waits for command, responds
    std::thread slave([&ch]() {
        uint16_t cmd_id;
        uint8_t payload[256], len;
        EXPECT_TRUE(ch.slave_wait_command(cmd_id, payload, len));
        EXPECT_EQ(cmd_id, SYSTEM_PING_COMMAND);
        ch.slave_send_response(cmd_id, 0x00, nullptr, 0);  // OK
    });

    // Master: sends command, waits for response
    ch.master_send_command(SYSTEM_PING_COMMAND, nullptr, 0);
    uint16_t resp_id;
    uint8_t resp_payload[256], resp_len;
    EXPECT_TRUE(ch.master_receive_response(resp_id, resp_payload, resp_len, 1000));
    EXPECT_EQ(resp_id, SYSTEM_PING_COMMAND);
    EXPECT_EQ(resp_len, 1u);      // 1 byte status
    EXPECT_EQ(resp_payload[0], 0x00);  // SPI_STATUS_OK

    slave.join();
}

TEST(SPIBridgeChannel, PayloadRoundTrip) {
    SPIBridgeChannel ch;

    uint8_t test_data[64];
    for (int i = 0; i < 64; i++) test_data[i] = (uint8_t)i;

    std::thread slave([&ch, &test_data]() {
        uint16_t cmd_id;
        uint8_t payload[256], len;
        EXPECT_TRUE(ch.slave_wait_command(cmd_id, payload, len));
        EXPECT_EQ(len, 64u);
        EXPECT_EQ(memcmp(payload, test_data, 64), 0);

        // Echo back
        ch.slave_send_response(cmd_id, 0x00, payload, len);
    });

    ch.master_send_command(TEST_COMMAND, test_data, 64);
    uint16_t resp_id;
    uint8_t resp_payload[256], resp_len;
    EXPECT_TRUE(ch.master_receive_response(resp_id, resp_payload, resp_len, 1000));
    EXPECT_EQ(resp_id, TEST_COMMAND);
    EXPECT_EQ(resp_len, 65u);  // 1 status + 64 payload
    EXPECT_EQ(resp_payload[0], 0x00);
    EXPECT_EQ(memcmp(resp_payload + 1, test_data, 64), 0);

    slave.join();
}

TEST(SPIBridgeChannel, TimeoutWhenNoResponse) {
    SPIBridgeChannel ch;

    ch.master_send_command(SYSTEM_PING_COMMAND, nullptr, 0);
    uint16_t resp_id;
    uint8_t resp_payload[256], resp_len;
    EXPECT_FALSE(ch.master_receive_response(resp_id, resp_payload, resp_len, 100));

    // Wait for the dangling command to be consumed by the channel
    // (no slave to consume it, so we just reset)
}

TEST(SPIBridgeChannel, StreamPushPop) {
    SPIBridgeChannel ch;

    uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    EXPECT_TRUE(ch.stream_push(TEST_STREAM, data, 4));
    EXPECT_TRUE(ch.stream_push(TEST_STREAM, data, 4));
    EXPECT_TRUE(ch.stream_push(TEST_STREAM, data, 4));

    uint8_t out_data[256];
    size_t out_len;
    uint16_t out_id;

    EXPECT_TRUE(ch.stream_pop(out_id, out_data, out_len));
    EXPECT_EQ(out_id, TEST_STREAM);
    EXPECT_EQ(out_len, 4u);

    EXPECT_TRUE(ch.stream_pop(out_id, out_data, out_len));
    EXPECT_TRUE(ch.stream_pop(out_id, out_data, out_len));
    EXPECT_FALSE(ch.stream_pop(out_id, out_data, out_len));  // empty
}

TEST(SPIBridgeChannel, StreamOverflow) {
    SPIBridgeChannel ch;

    uint8_t data[4] = {1, 2, 3, 4};
    for (int i = 0; i < 8; i++) {
        EXPECT_TRUE(ch.stream_push(TEST_STREAM, data, 4));
    }
    // 9th should fail
    EXPECT_FALSE(ch.stream_push(TEST_STREAM, data, 4));
}

TEST(SPIBridgeChannel, IRQSimulation) {
    SPIBridgeChannel ch;

    // Slave raises IRQ
    std::thread slave([&ch]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ch.slave_notify_irq();
    });

    // Master waits for IRQ
    EXPECT_TRUE(ch.master_wait_irq(1000));

    slave.join();

    // Second wait without new IRQ should timeout
    EXPECT_FALSE(ch.master_wait_irq(100));
}

TEST(SPIBridgeChannel, CloseWakesAllWaitersWithoutSignalingIRQ) {
    SPIBridgeChannel ch;

    std::promise<void> command_started;
    std::promise<void> response_started;
    std::promise<void> irq_started;
    auto command_ready = command_started.get_future();
    auto response_ready = response_started.get_future();
    auto irq_ready = irq_started.get_future();
    bool command_received = true;
    bool response_received = true;
    bool irq_received = true;

    std::thread command_waiter([&]() {
        uint16_t cmd_id = 0;
        uint8_t payload[SPI_MAX_PAYLOAD] = {};
        uint8_t len = 0;
        command_started.set_value();
        command_received = ch.slave_wait_command(cmd_id, payload, len);
    });
    std::thread response_waiter([&]() {
        uint16_t response_id = 0;
        uint8_t payload[SPI_MAX_PAYLOAD] = {};
        uint8_t len = 0;
        response_started.set_value();
        response_received =
            ch.master_receive_response(response_id, payload, len, 5000);
    });
    std::thread irq_waiter([&]() {
        irq_started.set_value();
        irq_received = ch.master_wait_irq(5000);
    });

    command_ready.wait();
    response_ready.wait();
    irq_ready.wait();
    ch.close();

    command_waiter.join();
    response_waiter.join();
    irq_waiter.join();
    EXPECT_FALSE(command_received);
    EXPECT_FALSE(response_received);
    EXPECT_FALSE(irq_received);
}

TEST(SPIBridgeChannel, ClosedChannelRejectsNewWork) {
    SPIBridgeChannel ch;
    ch.close();

    const uint8_t data[] = {1, 2, 3};
    ch.master_send_command(TEST_COMMAND, data, sizeof(data));
    ch.slave_send_response(TEST_COMMAND, 0, data, sizeof(data));
    ch.slave_notify_irq();

    uint16_t id = 0;
    uint8_t payload[SPI_MAX_PAYLOAD] = {};
    uint8_t len = 0;
    size_t stream_len = 0;
    EXPECT_FALSE(ch.slave_wait_command(id, payload, len));
    EXPECT_FALSE(ch.master_receive_response(id, payload, len, 0));
    EXPECT_FALSE(ch.master_wait_irq(0));
    EXPECT_FALSE(ch.stream_push(TEST_STREAM, data, sizeof(data)));
    EXPECT_FALSE(ch.stream_pop(id, payload, stream_len));
}

TEST(SPIBridgeChannel, ResponseLengthDoesNotOverflow) {
    SPIBridgeChannel ch;
    uint8_t data[UINT8_MAX];
    for (size_t index = 0; index < sizeof(data); ++index) {
        data[index] = static_cast<uint8_t>(index);
    }

    ch.slave_send_response(TEST_COMMAND, 0x7F, data, sizeof(data));

    uint16_t id = 0;
    uint8_t payload[SPI_MAX_PAYLOAD] = {};
    uint8_t len = 0;
    ASSERT_TRUE(ch.master_receive_response(id, payload, len, 0));
    EXPECT_EQ(id, TEST_COMMAND);
    EXPECT_EQ(len, UINT8_MAX);
    EXPECT_EQ(payload[0], 0x7F);
    EXPECT_EQ(payload[UINT8_MAX - 1], data[UINT8_MAX - 2]);
}

TEST(SPIBridgeChannel, FullStreamQueueDoesNotRaiseSpuriousIRQ) {
    SPIBridgeChannel ch;
    const uint8_t data[] = {1};
    for (size_t index = 0; index < STREAM_QUEUE_LEN; ++index) {
        ASSERT_TRUE(ch.stream_push(TEST_STREAM, data, sizeof(data)));
    }

    ch.slave_send_stream(TEST_STREAM, data, sizeof(data));

    EXPECT_FALSE(ch.master_wait_irq(0));
}
