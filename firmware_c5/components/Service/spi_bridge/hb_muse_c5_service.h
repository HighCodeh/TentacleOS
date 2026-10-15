/* Local SPI service only. No UART/host-link/network control surface. */
#ifndef HB_MUSE_C5_SERVICE_H
#define HB_MUSE_C5_SERVICE_H
#include "spi_bridge.h"
void hb_muse_c5_service_register(void);
spi_status_t hb_muse_c5_service_execute(
    uint16_t command, const uint8_t *input, uint8_t bytes, uint8_t *output, uint8_t *written);
void hb_muse_c5_service_revoke(void);
bool hb_muse_c5_service_allows(uint16_t command);
#endif
