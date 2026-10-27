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

#ifndef IR_AC_REQUEST_H
#define IR_AC_REQUEST_H

#include "ir_ac.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IR_AC_REQUEST_TEMP_MIN                      16
#define IR_AC_REQUEST_TEMP_MIN_COOLIX_MIDEA_TOSHIBA 17
#define IR_AC_REQUEST_TEMP_MAX                      30
#define IR_AC_REQUEST_GREE_AUTO_TEMP                25

/* These limits follow the existing encoders, not every product sold by a brand. */
static inline const char *ir_ac_request_error(const ir_ac_state_t *state) {
  if (state == NULL)
    return "Invalid AC state";
  if (state->protocol <= IR_AC_PROTO_UNKNOWN || state->protocol >= IR_AC_PROTO_COUNT)
    return "Choose a supported brand";
  if (state->mode < IR_AC_MODE_AUTO || state->mode > IR_AC_MODE_FAN ||
      state->fan < IR_AC_FAN_AUTO || state->fan > IR_AC_FAN_HIGH)
    return "Unsupported mode or fan";
  if (!state->power)
    return NULL;

  int min_temp = IR_AC_REQUEST_TEMP_MIN;
  if (state->protocol == IR_AC_PROTO_COOLIX || state->protocol == IR_AC_PROTO_MIDEA ||
      state->protocol == IR_AC_PROTO_TOSHIBA)
    min_temp = IR_AC_REQUEST_TEMP_MIN_COOLIX_MIDEA_TOSHIBA;
  if (state->temp_c < min_temp || state->temp_c > IR_AC_REQUEST_TEMP_MAX)
    return min_temp == IR_AC_REQUEST_TEMP_MIN ? "Supported range: 16-30 C"
                                              : "Supported range: 17-30 C";
  if (state->protocol == IR_AC_PROTO_GREE) {
    if (state->mode == IR_AC_MODE_AUTO && state->temp_c != IR_AC_REQUEST_GREE_AUTO_TEMP)
      return "GREE Auto requires 25 C";
    if (state->mode == IR_AC_MODE_DRY && state->fan != IR_AC_FAN_LOW)
      return "GREE Dry requires Low fan";
  }
  return NULL;
}

#ifdef __cplusplus
}
#endif
#endif
