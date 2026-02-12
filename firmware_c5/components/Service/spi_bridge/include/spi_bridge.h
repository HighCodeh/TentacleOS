#ifndef SPI_BRIDGE_C5_H
#define SPI_BRIDGE_C5_H

#include "spi_protocol.h"
#include "esp_err.h"

/**
 * @brief Inicializa o SPI Slave e o pino IRQ
 */
esp_err_t spi_bridge_slave_init(void);

/**
 * @brief Avisa o mestre (P4) que há dados prontos levantando o pino IRQ
 */
void spi_bridge_notify_master(void);

#endif // SPI_BRIDGE_C5_H
