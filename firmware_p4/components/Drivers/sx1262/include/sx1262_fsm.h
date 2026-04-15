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

#ifndef SX1262_FSM_H
#define SX1262_FSM_H

#include "esp_err.h"
#include "sx1262_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize FSM to a known state.
 */
void sx1262_fsm_init(sx1262_state_t initial_state);

/**
 * @brief Get current FSM state.
 */
sx1262_state_t sx1262_fsm_get_state(void);

/**
 * @brief Validate and execute a state transition.
 *
 * Based strictly on DS Fig. 9-1 transition diagram.
 * Returns ESP_ERR_INVALID_STATE if the transition is not allowed.
 *
 * @param target  Desired target state.
 * @return ESP_OK if transition is valid and state was updated.
 */
esp_err_t sx1262_fsm_transition(sx1262_state_t target);

/**
 * @brief Called after IRQ completes (TxDone, RxDone, Timeout).
 *
 * Sets state to fallback mode (STDBY_RC by default).
 * DS Section 13.1.15: SetRxTxFallbackMode.
 */
void sx1262_fsm_on_irq_complete(void);

/**
 * @brief Check if a transition from current state to target is valid.
 *
 * Does NOT modify state. For query/validation only.
 */
bool sx1262_fsm_can_transition(sx1262_state_t target);

#ifdef __cplusplus
}
#endif

#endif // SX1262_FSM_H
