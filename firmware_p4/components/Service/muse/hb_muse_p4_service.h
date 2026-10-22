// SPDX-License-Identifier: GPL-3.0-or-later
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

#ifndef FIRMWARE_P4_COMPONENTS_SERVICE_MUSE_HB_MUSE_P4_SERVICE_H
#define FIRMWARE_P4_COMPONENTS_SERVICE_MUSE_HB_MUSE_P4_SERVICE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef HB_MUSE_P4_HOST_TEST
#include "native_host.h"
#else
#include "esp_err.h"
#endif
/* Native task adapter; no SDK credential ever enters P4. Only local physical
 * UI may start these operations. Starting a task is not connection readiness. */
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HB_MUSE_P4_OFF,
  HB_MUSE_P4_CHAT,
  HB_MUSE_P4_SETUP,
  HB_MUSE_P4_TOKEN
} hb_muse_p4_operation_t;
typedef struct {
  bool active, ready, mic, error, confirming;
  uint8_t level, phase;
  uint64_t ticket;
  char status[64], detail[128], reply[1024];
} hb_muse_p4_view_t;
esp_err_t hb_muse_p4_start(hb_muse_p4_operation_t operation);
void hb_muse_p4_stop(void);
void hb_muse_p4_key(bool pressed); /* actual OK press/release only */
void hb_muse_p4_view(hb_muse_p4_view_t *out);
bool hb_muse_p4_is_active(void); /* includes cleanup, setup and token window */

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_P4_COMPONENTS_SERVICE_MUSE_HB_MUSE_P4_SERVICE_H
