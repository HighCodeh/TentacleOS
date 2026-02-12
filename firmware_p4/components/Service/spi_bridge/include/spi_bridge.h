#ifndef SPI_BRIDGE_H
#define SPI_BRIDGE_H

#include "spi_protocol.h"
#include "esp_err.h"

esp_err_t spi_bridge_master_init(void);
esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *payload, uint8_t len, 
                                 spi_header_t *out_header, uint8_t *out_payload, 
                                 uint32_t timeout_ms);

#endif