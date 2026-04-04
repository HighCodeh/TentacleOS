#ifndef SPI_BRIDGE_PHY_H
#define SPI_BRIDGE_PHY_H

#include "esp_err.h"
#include "driver/spi_master.h"
#include <stdint.h>
#include <stddef.h>

// Available GPSPI hosts on P4: SPI2_HOST, SPI3_HOST
#define BRIDGE_SPI_HOST  SPI2_HOST

esp_err_t spi_bridge_phy_init(void);
esp_err_t spi_bridge_phy_transmit(const uint8_t *tx_data, uint8_t *rx_data, size_t len);
esp_err_t spi_bridge_phy_wait_irq(uint32_t timeout_ms);

#endif