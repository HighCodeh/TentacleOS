// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
void meshtastic_mesh_stop(void);

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
 * @param request_id Se != 0, adiciona como Data.request_id (reply_id)
 * @param want_ack   Se true, seta flag want_ack no header
 */
esp_err_t meshtastic_mesh_send_data(uint32_t to,
                                    uint8_t channel,
                                    uint8_t hop_limit,
                                    uint8_t portnum,
                                    const uint8_t *payload,
                                    uint16_t plen,
                                    uint32_t request_id,
                                    bool want_ack);

#ifdef __cplusplus
}
#endif

#endif // MESHTASTIC_MESH_H
