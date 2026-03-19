/**
 * @file st25r3916_irq.c
 * @brief ST25R3916 interrupt status read and TX-end wait.
 */
#include "st25r3916_irq.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_gpio.h"
#include "hb_nfc_timer.h"

#include "esp_log.h"

static const char* TAG = "st25r_irq";

/**
 * Read IRQ status. Reading clears the flags.
 *  Order: ERROR, TIMER, MAIN, TARGET, COLLISION.
 */
st25r_irq_status_t st25r_irq_read(void)
{
    st25r_irq_status_t s = { 0 };
    hb_spi_reg_read(REG_ERROR_INT,     &s.error);
    hb_spi_reg_read(REG_TIMER_NFC_INT, &s.timer);
    hb_spi_reg_read(REG_MAIN_INT,      &s.main);
    hb_spi_reg_read(REG_TARGET_INT,    &s.target);
    hb_spi_reg_read(REG_COLLISION,      &s.collision);
    return s;
}

void st25r_irq_log(const char* ctx, uint16_t fifo_count)
{
    st25r_irq_status_t s = st25r_irq_read();
    ESP_LOGW(TAG, "%s IRQ: MAIN=0x%02X ERR=0x%02X TMR=0x%02X TGT=0x%02X COL=0x%02X FIFO=%u",
             ctx, s.main, s.error, s.timer, s.target, s.collision, fifo_count);
}

bool st25r_irq_wait_txe(void)
{
    /* Poll the physical IRQ pin (not the register).
     *
     * Reading REG_MAIN_INT in a tight loop clears the register on every
     * read, so RXS/RXE IRQs arriving during TX completion would be silently
     * consumed and the upper layer would never see them.
     *
     * Correct approach: wait for the pin to assert, then read all five IRQ
     * registers in one pass (st25r_irq_read clears all atomically).
     * If the IRQ was caused by something other than TXE (e.g. a field event),
     * keep waiting up to 20ms total.
     *
     * 10 µs poll interval (not 1 ms): in MIFARE Classic emulation, the reader
     * responds with {NR}{AR} starting ~87 µs after NT's last bit.  With 1 ms
     * polls, TXE detection is delayed ~1 ms past the actual event; by then the
     * reader's {NR}{AR} has arrived, and by the time we read it the reader has
     * timed out waiting for AT and restarted the protocol, clearing the FIFO.
     * 10 µs polls detect TXE within ~10 µs, leaving plenty of margin. */
    for (int i = 0; i < 2000; i++) {
        if (hb_gpio_irq_level()) {
            st25r_irq_status_t s = st25r_irq_read();
            if (s.main & IRQ_MAIN_TXE) return true;
            if (s.error) {
                ESP_LOGW(TAG, "TX error: ERR=0x%02X", s.error);
                return false;
            }
            /* Some other IRQ (field detect, etc.) — keep waiting. */
        }
        hb_delay_us(10);
    }
    ESP_LOGW(TAG, "TX timeout");
    return false;
}
