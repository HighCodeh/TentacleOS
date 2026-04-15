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

#include "sx1262_fsm.h"

#include "esp_log.h"

#define FSM_NUM_STATES 7

static const char *TAG = "SX1262_FSM";

static sx1262_state_t s_state = SX1262_STATE_UNKNOWN;

/*                               UNKN  SLEEP  RC    XOSC   FS    TX    RX   */
static const bool s_transition[FSM_NUM_STATES][FSM_NUM_STATES] = {
    /* FROM UNKNOWN    */ {false, false, true, false, false, false, false},
    /* FROM SLEEP      */ {false, false, true, false, false, false, false},
    /* FROM STDBY_RC   */ {false, true, false, true, true, true, true},
    /* FROM STDBY_XOSC */ {false, true, true, false, true, true, true},
    /* FROM FS         */ {false, true, true, true, false, true, true},
    /* FROM TX         */ {false, false, true, false, false, false, false},
    /* FROM RX         */ {false, false, true, false, false, false, false},
};

static const char *s_state_names[FSM_NUM_STATES] = {
    "UNKNOWN", "SLEEP", "STDBY_RC", "STDBY_XOSC", "FS", "TX", "RX"};

void sx1262_fsm_init(sx1262_state_t initial_state) {
  s_state = initial_state;
  ESP_LOGI(TAG, "FSM initialized → %s", s_state_names[s_state]);
}

sx1262_state_t sx1262_fsm_get_state(void) {
  return s_state;
}

bool sx1262_fsm_can_transition(sx1262_state_t target) {
  if (s_state >= FSM_NUM_STATES || target >= FSM_NUM_STATES) {
    return false;
  }
  return s_transition[s_state][target];
}

esp_err_t sx1262_fsm_transition(sx1262_state_t target) {
  if (s_state >= FSM_NUM_STATES || target >= FSM_NUM_STATES) {
    ESP_LOGE(TAG, "Invalid state index: current=%d, target=%d", s_state, target);
    return ESP_ERR_INVALID_STATE;
  }

  if (!s_transition[s_state][target]) {
    ESP_LOGE(TAG, "Transition NOT allowed: %s → %s", s_state_names[s_state], s_state_names[target]);
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGD(TAG, "%s → %s", s_state_names[s_state], s_state_names[target]);
  s_state = target;
  return ESP_OK;
}

void sx1262_fsm_on_irq_complete(void) {
  sx1262_state_t prev = s_state;
  s_state = SX1262_STATE_STDBY_RC;
  ESP_LOGD(TAG, "IRQ complete: %s → STDBY_RC", s_state_names[prev]);
}
