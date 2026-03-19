/**
 * @file hb_nfc_gpio.h
 * @brief HAL GPIO IRQ pin monitoring.
 *
 * The working code uses GPIO polling (not ISR) for IRQ.
 * We provide both: polling (proven) and ISR (optional upgrade).
 */
#ifndef HB_NFC_GPIO_H
#define HB_NFC_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

hb_nfc_err_t hb_gpio_init(int pin_irq);
void         hb_gpio_deinit(void);

/** Read IRQ pin level directly (0 or 1). Proven approach. */
int  hb_gpio_irq_level(void);

/** Wait for IRQ pin high with timeout (ms). Returns true if IRQ seen. */
bool hb_gpio_irq_wait(uint32_t timeout_ms);

#endif
