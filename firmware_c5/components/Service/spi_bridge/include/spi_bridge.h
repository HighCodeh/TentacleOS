#ifndef SPI_BRIDGE_C5_H
#define SPI_BRIDGE_C5_H

#include "spi_protocol.h"
#include "esp_err.h"

/**
 * @brief Inicializa o SPI Slave e o pino IRQ
 */
esp_err_t spi_bridge_slave_init(void);

/**
 * @brief Aponta o bridge para um conjunto de resultados em memória
 */
void spi_bridge_provide_results(void* source, uint16_t count, uint8_t item_size);
void spi_bridge_provide_results_dynamic(void* source, const uint16_t *count_ptr, uint8_t item_size);
bool spi_bridge_stream_is_enabled(spi_id_t id);
void spi_bridge_stream_enable(spi_id_t id, bool enable);
bool spi_bridge_stream_push(spi_id_t id, const uint8_t *data, uint8_t len);

/**
 * @brief Avisa o mestre (P4) que há dados prontos levantando o pino IRQ
 */
void spi_bridge_notify_master(void);

#endif // SPI_BRIDGE_C5_H
