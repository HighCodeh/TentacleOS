// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TOS_API_LORA_H
#define TOS_API_LORA_H

#include "tos_api.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tos_api_lora_configure(const tos_lora_cfg_t *config);
esp_err_t tos_api_lora_send(const uint8_t *data, uint8_t len);
esp_err_t tos_api_lora_rx_start(uint32_t *out_session);
int tos_api_lora_rx_poll(tos_lora_packet_t *out);
esp_err_t tos_api_lora_mesh_start(uint8_t proto, uint32_t *out_session);
esp_err_t tos_api_lora_session_stop(uint32_t session);
void tos_api_lora_cancel_owner(void *task);

#ifdef __cplusplus
}
#endif
#endif
