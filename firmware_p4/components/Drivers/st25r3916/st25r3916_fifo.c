/**
 * @file st25r3916_fifo.c
 * @brief ST25R3916 FIFO control and TX byte count setup.
 */
#include "st25r3916_fifo.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"

/**
 * FIFO count:
 *  lsb = REG_FIFO_STATUS1
 *  msb = REG_FIFO_STATUS2
 *  count = ((msb & 0xC0) << 2) | lsb   (10-bit value, max 512)
 */
uint16_t st25r_fifo_count(void)
{
    uint8_t lsb, msb;
    hb_spi_reg_read(REG_FIFO_STATUS1, &lsb);
    hb_spi_reg_read(REG_FIFO_STATUS2, &msb);
    return (uint16_t)(((msb & 0xC0) << 2) | lsb);
}

void st25r_fifo_clear(void)
{
    hb_spi_direct_cmd(CMD_CLEAR_FIFO);
}

hb_nfc_err_t st25r_fifo_load(const uint8_t* data, size_t len)
{
    return hb_spi_fifo_load(data, len);
}

hb_nfc_err_t st25r_fifo_read(uint8_t* data, size_t len)
{
    return hb_spi_fifo_read(data, len);
}

/**
 * Set TX byte count:
 *  reg1 = (nbytes >> 5) & 0xFF
 *  reg2 = ((nbytes & 0x1F) << 3) | (nbtx_bits & 0x07)
 */
void st25r_set_tx_bytes(uint16_t nbytes, uint8_t nbtx_bits)
{
    uint8_t reg1 = (uint8_t)((nbytes >> 5) & 0xFF);
    uint8_t reg2 = (uint8_t)(((nbytes & 0x1F) << 3) | (nbtx_bits & 0x07));
    hb_spi_reg_write(REG_NUM_TX_BYTES1, reg1);
    hb_spi_reg_write(REG_NUM_TX_BYTES2, reg2);
}

int st25r_fifo_wait(size_t min_bytes, int timeout_ms, uint16_t* final_count)
{
    uint16_t count = 0;
    for (int i = 0; i < timeout_ms; i++) {
        count = st25r_fifo_count();
        if (count >= min_bytes) {
            if (final_count) *final_count = count;
            return (int)count;
        }
        hb_delay_us(1000);
    }
    count = st25r_fifo_count();
    if (final_count) *final_count = count;
    return (int)count;
}
