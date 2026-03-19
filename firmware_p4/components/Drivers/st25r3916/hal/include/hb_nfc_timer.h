/**
 * @file hb_nfc_timer.h
 * @brief HAL Timer delay utilities.
 *
 * The working code uses esp_rom_delay_us() for all timing.
 * We wrap it for portability.
 */
#ifndef HB_NFC_TIMER_H
#define HB_NFC_TIMER_H

#include <stdint.h>

/** Busy-wait delay in microseconds. Uses ROM function (proven). */
void hb_delay_us(uint32_t us);

/** Busy-wait delay in milliseconds. */
void hb_delay_ms(uint32_t ms);

#endif
