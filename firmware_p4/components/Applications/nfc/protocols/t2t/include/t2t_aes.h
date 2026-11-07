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

#ifndef T2T_AES_H
#define T2T_AES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define T2T_AES_BLOCK_SIZE         16U
#define T2T_AES_MAC_SIZE           8U
#define T2T_AES_DATA_MAX           256U
#define T2T_AES_AUTH_REPLY_SIZE    (1U + T2T_AES_BLOCK_SIZE)
#define T2T_AES_AUTH_CONTINUE_SIZE (1U + 2U * T2T_AES_BLOCK_SIZE)

typedef struct {
  uint8_t key[T2T_AES_BLOCK_SIZE];
  uint8_t nonce[T2T_AES_BLOCK_SIZE];
  uint8_t mac_key[T2T_AES_BLOCK_SIZE];
  uint16_t counter;
  uint8_t key_number;
  bool is_pending;
  bool is_authenticated;
  bool is_secure;
} t2t_aes_session_t;

esp_err_t t2t_aes_reader_challenge(t2t_aes_session_t *session,
                                   const uint8_t *key,
                                   const uint8_t *nonce,
                                   const uint8_t *reply,
                                   size_t length,
                                   bool secure,
                                   uint8_t *out_frame);
esp_err_t t2t_aes_reader_complete(t2t_aes_session_t *session, const uint8_t *reply, size_t length);
void t2t_aes_reset(t2t_aes_session_t *session);
esp_err_t t2t_aes_begin(t2t_aes_session_t *session,
                        const uint8_t key[T2T_AES_BLOCK_SIZE],
                        const uint8_t nonce[T2T_AES_BLOCK_SIZE],
                        uint8_t out_reply[T2T_AES_AUTH_REPLY_SIZE]);
esp_err_t t2t_aes_finish(t2t_aes_session_t *session,
                         const uint8_t *frame,
                         size_t length,
                         bool secure,
                         uint8_t out_reply[T2T_AES_AUTH_REPLY_SIZE]);
esp_err_t
t2t_aes_verify(t2t_aes_session_t *session, const uint8_t *frame, size_t length, size_t *out_length);
esp_err_t t2t_aes_sign(t2t_aes_session_t *session,
                       const uint8_t *data,
                       size_t length,
                       uint8_t out_mac[T2T_AES_MAC_SIZE]);

#ifdef __cplusplus
}
#endif
#endif
