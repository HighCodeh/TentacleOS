#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <map>

namespace hle {

enum CC1101State { CC_IDLE = 0, CC_RX, CC_TX, CC_FSTXON, CC_CALIBRATE, CC_SETTLING };

class CC1101Emulator {
public:
    CC1101Emulator() { reset(); }

    void reset() {
        memset(m_regs, 0, sizeof(m_regs));
        m_tx_fifo.clear();
        m_rx_fifo.clear();
        m_state = CC_IDLE;
        m_gdo0 = false;
        m_gdo2 = false;
        m_chip_status = 0x0F; // IDLE
        init_defaults();
    }

    // SPI read: returns chip status byte + data byte (burst = multiple)
    uint8_t read_register(uint8_t addr) {
        uint8_t burst = (addr & 0xC0) >> 6;
        addr &= 0x3F;

        if (addr == 0x3E) { // PATABLE (burst read will iterate)
            return m_patable[0];
        }
        if (addr == 0x3F) { // TX FIFO read
            if (!m_tx_fifo.empty()) {
                uint8_t v = m_tx_fifo.front();
                m_tx_fifo.pop_front();
                return v;
            }
            return 0;
        }
        if (addr <= 0x2E) return m_regs[addr];
        if (addr >= 0x30) return m_chip_status; // status regs return chip status
        return 0;
    }

    // SPI write: addr & 0x3F with optional burst bit
    void write_register(uint8_t addr, uint8_t data) {
        bool burst = (addr & 0x40) != 0;
        addr &= 0x3F;

        if (addr == 0x3E) { // PATABLE
            m_patable[0] = data;
            return;
        }
        if (addr == 0x3F) { // TX FIFO
            if (m_tx_fifo.size() < 64) m_tx_fifo.push_back(data);
            return;
        }
        if (addr <= 0x2E) m_regs[addr] = data;

        // Handle burst writes
        (void)burst;
    }

    // Command strobe
    void strobe(uint8_t addr) {
        addr &= 0x3F;
        switch (addr) {
        case 0x30: m_state = CC_IDLE; break;
        case 0x34: m_state = CC_TX; m_chip_status = 0x02; break;
        case 0x36: m_state = CC_RX; m_chip_status = 0x01; break;
        case 0x3A: flush_rx_fifo(); break;
        case 0x3B: flush_tx_fifo(); break;
        default: break;
        }
    }

    uint8_t chip_status() const { return m_chip_status; }
    CC1101State state() const { return m_state; }
    bool gdo0() const { return m_gdo0; }
    bool gdo2() const { return m_gdo2; }

    void set_gdo0(bool v) { m_gdo0 = v; }
    void set_gdo2(bool v) { m_gdo2 = v; }

    // For testing: inject received data into RX FIFO
    void inject_rx_data(const uint8_t *data, size_t len) {
        for (size_t i = 0; i < len && m_rx_fifo.size() < 64; i++)
            m_rx_fifo.push_back(data[i]);
    }

    const uint8_t *registers() const { return m_regs; }

    // Preset configuration
    void apply_preset(const char *preset) {
        (void)preset;
        // Store preset-like values in regs 0x00-0x0B
        m_regs[0x00] = 0x0C; // IOCFG2
        m_regs[0x02] = 0x06; // IOCFG0
        m_regs[0x06] = 0x20; // PKTCTRL0
        m_regs[0x07] = 0x04; // PKTCTRL1
        m_regs[0x0B] = 0x06; // FSCTRL1
    }

    // Frequency set via registers 0x0D-0x0F (FREQ2, FREQ1, FREQ0)
    void set_frequency_mhz(float mhz) {
        uint32_t f = (uint32_t)((mhz / 26.0f) * 65536.0f);
        m_regs[0x0D] = (f >> 16) & 0xFF;
        m_regs[0x0E] = (f >> 8) & 0xFF;
        m_regs[0x0F] = f & 0xFF;
    }

    float frequency_mhz() const {
        uint32_t f = ((uint32_t)m_regs[0x0D] << 16) | ((uint32_t)m_regs[0x0E] << 8) | m_regs[0x0F];
        return (float)f * 26.0f / 65536.0f;
    }

private:
    void init_defaults() {
        m_regs[0x0D] = 0x10; // FREQ2
        m_regs[0x0E] = 0xB1; // FREQ1
        m_regs[0x0F] = 0x3B; // FREQ0 (~433.92 MHz)
        m_patable[0] = 0xC0;
    }

    void flush_rx_fifo() { m_rx_fifo.clear(); }
    void flush_tx_fifo() { m_tx_fifo.clear(); }

    uint8_t m_regs[0x30];
    uint8_t m_patable[8];
    std::deque<uint8_t> m_tx_fifo;
    std::deque<uint8_t> m_rx_fifo;
    CC1101State m_state;
    uint8_t m_chip_status;
    bool m_gdo0;
    bool m_gdo2;
};

}  // namespace hle
