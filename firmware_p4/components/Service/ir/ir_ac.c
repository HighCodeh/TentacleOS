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

#include "ir_ac.h"

#include <string.h>

#include "esp_log.h"

#include "ir.h"
#include "ir_ac_coolix.h"
#include "ir_ac_gree.h"
#include "ir_ac_lg.h"
#include "ir_ac_midea.h"
#include "ir_ac_toshiba.h"
#include "ir_ac_haier.h"

static const char *TAG = "IR_AC";

const char *ir_ac_protocol_name(ir_ac_protocol_t proto) {
  switch (proto) {
    case IR_AC_PROTO_COOLIX:
      return "COOLIX";
    case IR_AC_PROTO_GREE:
      return "GREE";
    case IR_AC_PROTO_LG:
      return "LG";
    case IR_AC_PROTO_MIDEA:
      return "MIDEA";
    case IR_AC_PROTO_TOSHIBA:
      return "TOSHIBA";
    case IR_AC_PROTO_HAIER:
      return "HAIER";
    default:
      return "UNKNOWN";
  }
}

const char *ir_ac_mode_name(ir_ac_mode_t mode) {
  switch (mode) {
    case IR_AC_MODE_COOL:
      return "Cool";
    case IR_AC_MODE_DRY:
      return "Dry";
    case IR_AC_MODE_HEAT:
      return "Heat";
    case IR_AC_MODE_FAN:
      return "Fan";
    case IR_AC_MODE_AUTO:
    default:
      return "Auto";
  }
}

const char *ir_ac_fan_name(ir_ac_fan_t fan) {
  switch (fan) {
    case IR_AC_FAN_LOW:
      return "Low";
    case IR_AC_FAN_MED:
      return "Med";
    case IR_AC_FAN_HIGH:
      return "High";
    case IR_AC_FAN_AUTO:
    default:
      return "Auto";
  }
}

uint32_t ir_ac_carrier_freq(ir_ac_protocol_t proto) {
  switch (proto) {
    case IR_AC_PROTO_COOLIX:
      return COOLIX_CARRIER_HZ;
    case IR_AC_PROTO_GREE:
      return GREE_CARRIER_HZ;
    case IR_AC_PROTO_LG:
      return LGAC_CARRIER_HZ;
    case IR_AC_PROTO_MIDEA:
      return MIDEA_CARRIER_HZ;
    case IR_AC_PROTO_TOSHIBA:
      return TOSHIBA_CARRIER_HZ;
    case IR_AC_PROTO_HAIER:
      return HAIER_CARRIER_HZ;
    default:
      return COOLIX_CARRIER_HZ;
  }
}

size_t ir_ac_encode(const ir_ac_state_t *state, rmt_symbol_word_t *symbols, size_t max) {
  if (state == NULL || symbols == NULL || max == 0)
    return 0;

  switch (state->protocol) {
    case IR_AC_PROTO_COOLIX:
      return ir_ac_coolix_encode(state, symbols, max);
    case IR_AC_PROTO_GREE:
      return ir_ac_gree_encode(state, symbols, max);
    case IR_AC_PROTO_LG:
      return ir_ac_lg_encode(state, symbols, max);
    case IR_AC_PROTO_MIDEA:
      return ir_ac_midea_encode(state, symbols, max);
    case IR_AC_PROTO_TOSHIBA:
      return ir_ac_toshiba_encode(state, symbols, max);
    case IR_AC_PROTO_HAIER:
      return ir_ac_haier_encode(state, symbols, max);
    default:
      ESP_LOGW(TAG, "Encode called with unknown AC protocol: %d", (int)state->protocol);
      return 0;
  }
}

esp_err_t ir_ac_send(const ir_ac_state_t *state) {
  if (state == NULL)
    return ESP_ERR_INVALID_ARG;

  rmt_symbol_word_t symbols[IR_MAX_SYMBOLS];
  size_t count = ir_ac_encode(state, symbols, IR_MAX_SYMBOLS);
  if (count == 0)
    return ESP_ERR_INVALID_ARG;

  return ir_send_raw(symbols, count, ir_ac_carrier_freq(state->protocol));
}

bool ir_ac_decode(const rmt_symbol_word_t *symbols, size_t count, ir_ac_state_t *out_state) {
  if (symbols == NULL || count == 0 || out_state == NULL)
    return false;

  memset(out_state, 0, sizeof(ir_ac_state_t));

  if (ir_ac_coolix_decode(symbols, count, out_state))
    return true;
  if (ir_ac_gree_decode(symbols, count, out_state))
    return true;
  if (ir_ac_lg_decode(symbols, count, out_state))
    return true;
  if (ir_ac_midea_decode(symbols, count, out_state))
    return true;
  if (ir_ac_toshiba_decode(symbols, count, out_state))
    return true;
  if (ir_ac_haier_decode(symbols, count, out_state))
    return true;

  out_state->protocol = IR_AC_PROTO_UNKNOWN;
  return false;
}
