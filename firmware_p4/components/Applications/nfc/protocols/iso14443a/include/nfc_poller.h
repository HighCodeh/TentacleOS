/**
 * @file nfc_poller.h
 * @brief NFC Poller field control + transceive engine.
 *
 * The transceive function is the direct refactor of the working code's
 * st25r_transceive(). It handles: clear FIFO set bytes load FIFO
 *  send cmd wait TXE wait RX FIFO read result.
 */
#ifndef NFC_POLLER_H
#define NFC_POLLER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

/** Initialize poller: set NFC-A mode + field on. */
hb_nfc_err_t nfc_poller_start(void);

/** Stop poller: field off. */
void nfc_poller_stop(void);

/**
 * Transceive direct refactor of working code st25r_transceive():
 *
 *  1. CMD_CLEAR_FIFO
 *  2. st25r_set_tx_bytes(tx_len, 0)
 *  3. st25r_fifo_load(tx, tx_len)
 *  4. CMD_TX_WITH_CRC or CMD_TX_WO_CRC
 *  5. Poll MAIN_INT for TXE (50us 400 = 20ms)
 *  6. Wait for rx_min bytes in FIFO
 *  7. Read FIFO
 *
 * @param tx Data to transmit.
 * @param tx_len TX length in bytes.
 * @param with_crc true = CMD_TX_WITH_CRC, false = CMD_TX_WO_CRC.
 * @param rx Buffer for received data.
 * @param rx_max Max RX buffer size.
 * @param rx_min Minimum expected RX bytes (0 = don't wait).
 * @param timeout_ms RX FIFO wait timeout.
 * @return Number of bytes received, 0 on failure.
 */
int nfc_poller_transceive(const uint8_t* tx, size_t tx_len, bool with_crc,
                           uint8_t* rx, size_t rx_max, size_t rx_min,
                           int timeout_ms);

/** Configure guard time and FDT validation (best-effort). */
void nfc_poller_set_timing(uint32_t guard_time_us, uint32_t fdt_min_us, bool validate_fdt);

/** Get last measured FDT in microseconds. */
uint32_t nfc_poller_get_last_fdt_us(void);

#endif
