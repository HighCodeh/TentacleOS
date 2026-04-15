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

#ifndef SX1262_IRQ_H
#define SX1262_IRQ_H

#include "esp_err.h"
#include "sx1262_types.h"
#include "sx1262_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Ring Buffer Config ───────────────────────────────────────────── */
#define SX1262_RX_RING_SIZE 8

/**
 * @brief Initialize IRQ subsystem. Resets ring buffer.
 */
void sx1262_irq_init(sx1262_hal_t *hal,
                     const sx1262_config_t *config,
                     const sx1262_callbacks_t *cbs);

/**
 * @brief Process pending IRQ flags.
 *
 * Called by ISR task when DIO1 rises. Never from ISR directly.
 * Sequence: GetIrqStatus → ClearIrqStatus → process → callbacks → FSM update.
 *
 * @return ESP_OK on success.
 */
esp_err_t sx1262_irq_process(void);

/**
 * @brief Dequeue next received packet from ring buffer.
 *
 * Thread-safe via enter_critical/exit_critical.
 *
 * @param out_packet  Destination for packet data. Must not be NULL.
 * @return ESP_OK if packet available, ESP_ERR_NOT_FOUND if empty.
 */
esp_err_t sx1262_irq_get_packet(sx1262_packet_t *out_packet);

/**
 * @brief Check if ring buffer has packets available.
 */
bool sx1262_irq_has_packet(void);

#ifdef __cplusplus
}
#endif

#endif // SX1262_IRQ_H
