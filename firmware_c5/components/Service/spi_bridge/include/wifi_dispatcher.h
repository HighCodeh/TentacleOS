#ifndef WIFI_DISPATCHER_H
#define WIFI_DISPATCHER_H

#include "spi_protocol.h"
#include "esp_err.h"

/**
 * @brief Processa um comando de WiFi recebido via SPI
 * 
 * @param id ID do comando
 * @param payload Dados do comando
 * @param len Tamanho do payload
 * @param out_resp_payload Buffer para preencher com a resposta
 * @param out_resp_len Tamanho da resposta gerada
 * @return spi_status_t Status da operação
 */
spi_status_t wifi_dispatcher_execute(spi_id_t id, const uint8_t *payload, uint8_t len, 
                                   uint8_t *out_resp_payload, uint8_t *out_resp_len);

#endif // WIFI_DISPATCHER_H
