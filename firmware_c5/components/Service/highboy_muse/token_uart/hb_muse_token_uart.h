/* Local C5 UART token loading, explicitly armed by the physical Muse screen.
 * No token in commands, firmware, logs, NVS, P4, or network. */
#ifndef HB_MUSE_TOKEN_UART_H
#define HB_MUSE_TOKEN_UART_H
#include <stdbool.h>
#include <stddef.h>
/* Store callback checks this atomic/time-only guard INSIDE its copy lock.
 * Guard has no quiet/setup callback, so no recursive lock acquisition. */
#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*hb_muse_token_store_fn)(const char *, size_t, bool (*)(void *), void *);
/* Task context; caller serializes ARM against OTA/UART ownership. Rejects an
 * already installed UART0 driver. quiet() must remain true throughout receive.
 * No automatic retries. Reset clears the RAM-only token. */
bool hb_muse_token_uart_arm(hb_muse_token_store_fn store, bool (*quiet)(void));
bool hb_muse_token_uart_active(void);
void hb_muse_token_uart_cancel(void);
#ifdef __cplusplus
}
#endif

#endif
