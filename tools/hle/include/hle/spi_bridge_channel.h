#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <queue>
#include <vector>

namespace hle {

static constexpr size_t SPI_HEADER_SIZE = 5;
static constexpr size_t SPI_MAX_PAYLOAD = 256;
static constexpr size_t SPI_FRAME_SIZE =
    (SPI_HEADER_SIZE + SPI_MAX_PAYLOAD + 3) & ~static_cast<size_t>(3);
static constexpr uint8_t SPI_SYNC_BYTE = 0xAA;
static constexpr size_t STREAM_QUEUE_LEN = 8;
static constexpr uint8_t SPI_MAX_RESPONSE_DATA = 254;

struct SPIFrame {
    uint8_t sync;
    uint8_t type;
    uint8_t category;
    uint8_t op;
    uint8_t length;
    uint8_t payload[SPI_MAX_PAYLOAD];

    SPIFrame() : sync(0), type(0), category(0), op(0), length(0) {
        memset(payload, 0, sizeof(payload));
    }

    bool is_valid() const { return sync == SPI_SYNC_BYTE; }

    uint16_t command_id() const {
        return static_cast<uint16_t>((static_cast<uint16_t>(category) << 8) | op);
    }

    void build_header(uint8_t frame_type, uint16_t command_id, uint8_t payload_len) {
        sync = SPI_SYNC_BYTE;
        type = frame_type;
        category = static_cast<uint8_t>(command_id >> 8);
        op = static_cast<uint8_t>(command_id);
        length = payload_len;
    }
};

class SPIBridgeChannel {
public:
    SPIBridgeChannel();
    ~SPIBridgeChannel();

    // ── Master (P4) side ─────────────────────────────────────────────────

    void master_send_command(uint16_t command_id, const uint8_t *payload, uint8_t payload_len);
    bool master_receive_response(uint16_t &out_command_id, uint8_t *out_payload,
                                 uint8_t &out_len, uint32_t timeout_ms);

    // ── Slave (C5) side ──────────────────────────────────────────────────

    bool slave_wait_command(uint16_t &out_command_id, uint8_t *out_payload,
                            uint8_t &out_len);
    void slave_send_response(uint16_t command_id, uint8_t status, const uint8_t *payload,
                             uint8_t payload_len);
    void slave_send_stream(uint16_t stream_id, const uint8_t *payload, uint8_t payload_len);

    // ── Stream queue (C5 → P4) ───────────────────────────────────────────

    bool stream_push(uint16_t stream_id, const uint8_t *data, size_t len);
    bool stream_pop(uint16_t &out_stream_id, uint8_t *out_data, size_t &out_len);

    // ── IRQ simulation ───────────────────────────────────────────────────

    void slave_notify_irq();
    bool master_wait_irq(uint32_t timeout_ms);

    // ── Management ───────────────────────────────────────────────────────

    void reset();
    void close();
    bool is_closed() const { return s_closed.load(std::memory_order_acquire); }

private:
    std::atomic<bool> s_closed{false};

    // Master → Slave command queue
    SPIFrame s_command_frame;
    bool s_has_command = false;
    std::mutex s_cmd_mutex;
    std::condition_variable s_cmd_cv;

    // Slave → Master response
    SPIFrame s_response_frame;
    bool s_has_response = false;
    std::mutex s_resp_mutex;
    std::condition_variable s_resp_cv;

    // IRQ flag
    bool s_irq_raised = false;
    std::mutex s_irq_mutex;
    std::condition_variable s_irq_cv;

    // Stream queue (C5 pushes, P4 pops)
    struct StreamItem {
        uint16_t stream_id;
        uint8_t data[SPI_MAX_PAYLOAD];
        size_t len;
    };
    std::deque<StreamItem> s_stream_queue;
    std::mutex s_stream_mutex;
};

}  // namespace hle

// Global bridge channel pointer for firmware shims
extern "C" {
void hle_set_bridge_channel(hle::SPIBridgeChannel *ch);
hle::SPIBridgeChannel *hle_get_bridge_channel(void);
}
