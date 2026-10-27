// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef MESHTASTIC_MESH_H
#define MESHTASTIC_MESH_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Meshtastic mesh radio layer.
 *
 * Sets up crypto, dedup table, and prepares for RX/TX.
 *
 * @param node_num  Our node number.
 * @return ESP_OK on success.
 */
esp_err_t meshtastic_mesh_init(uint32_t node_num);

/**
 * @brief Start continuous RX and mesh processing.
 *
 * Enters RX mode. Received packets are decrypted, deduped,
 * forwarded to the app, and rebroadcast if needed.
 *
 * @return ESP_OK on success.
 */
esp_err_t meshtastic_mesh_start(void);

/**
 * @brief Stop mesh processing.
 */
esp_err_t meshtastic_mesh_stop(void);

/**
 * @brief Process a ToRadio MeshPacket from the app.
 *
 * Encrypts and transmits via radio.
 *
 * @param pb_data  Raw protobuf bytes of the MeshPacket (from ToRadio field 1).
 * @param pb_len   Length of pb_data.
 * @return ESP_OK on success.
 */
esp_err_t meshtastic_mesh_send(const uint8_t *pb_data, uint16_t pb_len);

/**
 * @brief Poll for mesh activity. Call from main loop.
 */
void meshtastic_mesh_poll(void);

/**
 * @brief Send NodeInfo announcement via LoRa.
 *
 * Broadcasts our identity so other nodes see us.
 * Call periodically (e.g. every 30s).
 *
 * @return ESP_OK on success.
 */
esp_err_t meshtastic_mesh_send_nodeinfo(void);

/**
 * @brief Send a text message via LoRa.
 *
 * Broadcasts a UTF-8 text string on the mesh.
 *
 * @param text  Null-terminated string to send.
 * @param to    Destination node (0xFFFFFFFF for broadcast).
 * @return ESP_OK on success.
 */
esp_err_t meshtastic_mesh_send_text(const char *text, uint32_t to);

/**
 * @brief Envia Data protobuf generico na mesh (usado pelos modulos).
 *
 * Constroi Data { portnum(1), payload(2), request_id(3?), want_response(4?) },
 * encripta com AES-CTR, e enfileira para TX.
 *
 * @param to         Node destino (0xFFFFFFFF = broadcast)
 * @param channel    Indice do canal (0 = primary)
 * @param hop_limit  3 default
 * @param portnum    Meshtastic PortNum
 * @param payload    Bytes do payload (proto da aplicacao)
 * @param plen       Tamanho
 * @param request_id    Se != 0, adiciona como Data.request_id (reply_id)
 * @param want_ack      Se true, seta flag want_ack no header do MeshPacket
 * @param want_response Se true, seta Data.want_response (campo 3) - pede reply
 *                      do modulo destino (ex: NodeInfo request).
 */
esp_err_t meshtastic_mesh_send_data(uint32_t to,
                                    uint8_t channel,
                                    uint8_t hop_limit,
                                    uint8_t portnum,
                                    const uint8_t *payload,
                                    uint16_t plen,
                                    uint32_t request_id,
                                    bool want_ack,
                                    bool want_response);

/**
 * @brief Cancela uma retransmissao pendente porque o ACK chegou.
 *
 * Chamado pelo RoutingModule quando recebe Routing{error_reason=NONE} cujo
 * request_id case com um pacote em retry, ou por process_rx_packet quando
 * nosso proprio pacote eh ouvido sendo rebroadcastado (ACK implicito).
 *
 * @param pkt_id      Packet id original (MeshPacket.id) que foi transmitido.
 * @param is_implicit true se o ACK veio por audicao do rebroadcast.
 * @return true se havia um retry pendente e foi cancelado.
 */
bool meshtastic_mesh_retry_ack(uint32_t pkt_id, bool is_implicit);

#ifdef __cplusplus
}
#endif

#endif // MESHTASTIC_MESH_H
