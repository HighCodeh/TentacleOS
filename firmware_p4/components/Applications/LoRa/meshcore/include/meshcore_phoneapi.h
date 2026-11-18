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

#ifndef MESHCORE_PHONEAPI_H
#define MESHCORE_PHONEAPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#include "meshcore.h"

/**
 * @brief Outbound callback installed by the phone bridge.
 *
 * Invoked synchronously from inside command handlers and push helpers
 * with the raw frame that should be delivered to the connected phone.
 * The bridge is free to drop the frame if no transport is connected.
 *
 * @param ctx   Context registered together with the callback.
 * @param data  Raw frame bytes (1-byte tag + payload, MeshCore Companion).
 * @param len   Length in bytes (<= 240).
 */
typedef void (*meshcore_phoneapi_outbound_cb_t)(void *ctx, const uint8_t *data, uint16_t len);

/**
 * @brief Initializes the MeshCore companion protocol layer.
 *
 * Loads (or generates) the BLE pairing PIN from NVS. Does NOT touch any
 * radio peripherals — the phone transport (BLE / TCP) is owned by the
 * companion bridge on the C5.
 *
 * Must be called AFTER meshcore_init.
 *
 * @return
 *   - ESP_OK on success, including an already initialized protocol layer
 */
esp_err_t meshcore_phoneapi_init(void);

/**
 * @brief Registers the outbound bridge.
 *
 * @param cb   Function invoked when the protocol layer wants to send a
 *             frame to the phone. NULL detaches the bridge.
 * @param ctx  Opaque context forwarded to cb on every call.
 */
void meshcore_phoneapi_set_outbound(meshcore_phoneapi_outbound_cb_t cb, void *ctx);

/**
 * @brief Returns the BLE pairing PIN (6 digits).
 */
uint32_t meshcore_phoneapi_get_pin(void);

/**
 * @brief Notifies the protocol layer that the phone disconnected.
 *
 * Resets the negotiated app target version and drops any pending offline
 * messages so the next session starts clean.
 */
void meshcore_phoneapi_on_disconnect(void);

/**
 * @brief Processes a command frame received from the phone.
 *
 * Dispatches one of the CMD_* opcodes from the MeshCore Companion
 * protocol. Responses and pushes are produced synchronously via the
 * outbound callback registered with meshcore_phoneapi_set_outbound.
 *
 * @param buf  Frame bytes (first byte is the opcode).
 * @param len  Length in bytes.
 */
void meshcore_phoneapi_on_inbound(const uint8_t *buf, uint16_t len);

/**
 * @brief Pushes PUSH_CODE_NEW_ADVERT (0x8A) to the phone.
 *
 * @param contact  Newly announced contact.
 */
void meshcore_phoneapi_push_new_advert(const meshcore_contact_t *contact);

/**
 * @brief Queues a channel message and emits PUSH_CODE_MSG_WAITING (0x83).
 *
 * @param channel_idx  Channel slot.
 * @param path_len     Hops.
 * @param timestamp    Unix ts.
 * @param snr          SNR in dB.
 * @param text         Null-terminated UTF-8 text.
 */
void meshcore_phoneapi_push_channel_msg(
    uint8_t channel_idx, uint8_t path_len, uint32_t timestamp, int8_t snr, const char *text);

/**
 * @brief Queues a DM and emits PUSH_CODE_MSG_WAITING (0x83).
 *
 * @param peer_pub_key  Sender pubkey (32B).
 * @param path_len      Hops.
 * @param txt_type      Sub-type (MC_TXT_TYPE_*).
 * @param timestamp     Unix ts.
 * @param snr           SNR in dB.
 * @param text          UTF-8 text.
 */
void meshcore_phoneapi_push_contact_msg(const uint8_t peer_pub_key[32],
                                        uint8_t path_len,
                                        uint8_t txt_type,
                                        uint32_t timestamp,
                                        int8_t snr,
                                        const char *text);

/**
 * @brief Pushes PUSH_CODE_SEND_CONFIRMED (0x82) when an ACK arrives.
 *
 * @param ack_crc  ACK CRC.
 * @param snr      SNR of the ACK packet.
 */
void meshcore_phoneapi_push_send_confirmed(uint32_t ack_crc, uint8_t snr);

/**
 * @brief Pushes PUSH_CODE_PATH_UPDATED (0x81) when out_path is learned.
 *
 * @param contact  Contact with freshly updated path.
 */
void meshcore_phoneapi_push_path_updated(const meshcore_contact_t *contact);

#ifdef __cplusplus
}
#endif

#endif // MESHCORE_PHONEAPI_H
