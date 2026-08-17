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

#ifndef HOST_LINK_SEC_H
#define HOST_LINK_SEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Host-link security core (host-link internal). Implements the HMAC-SHA256/HKDF
// envelope from docs/host_link/protocol.md §6 over mbedTLS:
//   - PSK persisted in NVS (auto-generated on first boot).
//   - HELLO/HELLO_ACK handshake → per-direction session keys + counter reset.
//   - Per-frame MAC verify (inbound, K_a2d) / sign (outbound, K_d2a).
//   - Monotonic inbound counter (replay rejection).

#define HOST_LINK_PSK_SIZE     32
#define HOST_LINK_KEY_SIZE     32
#define HOST_LINK_PSK_HEX_SIZE (HOST_LINK_PSK_SIZE * 2 + 1)

/**
 * @brief Load the PSK from NVS (generating + persisting a random one if absent)
 *        and clear any session state. Call once at host-link init.
 */
esp_err_t host_link_sec_init(void);

/** @brief True once a HELLO handshake has established session keys. */
bool host_link_sec_is_authenticated(void);

/** @brief Drop the current session (e.g. on transport disconnect). */
void host_link_sec_reset(void);

/**
 * @brief Process an inbound HELLO payload and build the HELLO_ACK payload.
 *
 * Derives K_a2d/K_d2a, marks the session authenticated, and resets the inbound
 * counter baseline. The caller emits @p ack_out as an unauthenticated
 * HELLO_ACK frame.
 *
 * @param payload  HELLO payload: [host_ver u8][client_nonce[16]].
 * @param plen     Length of @p payload.
 * @param ack_out  Buffer for the HELLO_ACK payload.
 * @param ack_cap  Capacity of @p ack_out.
 * @param out_len  Receives the HELLO_ACK payload length.
 */
esp_err_t host_link_sec_handle_hello(
    const uint8_t *payload, uint16_t plen, uint8_t *ack_out, size_t ack_cap, size_t *out_len);

/**
 * @brief Verify an inbound authenticated frame: recompute the MAC over @p span
 *        with K_a2d, constant-time compare against @p mac, and require the
 *        counter to be strictly newer than the last accepted one.
 *
 * On success the inbound counter baseline is advanced.
 *
 * @param span      Bytes covered by the MAC ([VER .. end of BODY)).
 * @param span_len  Length of @p span.
 * @param mac       Received 16-byte MAC.
 * @param counter   Frame counter (from the envelope).
 * @return true if authentic and fresh.
 */
bool host_link_sec_verify_inbound(const uint8_t *span,
                                  size_t span_len,
                                  const uint8_t *mac,
                                  uint32_t counter);

/**
 * @brief Compute the outbound 16-byte MAC over @p span with K_d2a.
 *
 * @param span      Bytes to authenticate ([VER .. end of BODY)).
 * @param span_len  Length of @p span.
 * @param out_mac   Receives the 16-byte truncated HMAC.
 */
void host_link_sec_sign_outbound(const uint8_t *span, size_t span_len, uint8_t *out_mac);

/** @brief Copy the PSK as a lowercase hex string (for QR/console provisioning). */
esp_err_t host_link_sec_get_psk_hex(char *out, size_t out_cap);

/** @brief Generate, persist, and switch to a new random PSK (invalidates pairings). */
esp_err_t host_link_sec_regenerate_psk(void);

#ifdef __cplusplus
}
#endif

#endif // HOST_LINK_SEC_H
