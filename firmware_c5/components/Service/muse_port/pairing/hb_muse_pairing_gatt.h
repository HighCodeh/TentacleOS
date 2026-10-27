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

/* Passive registration in the EXISTING NimBLE owner. Never initializes/runs
 * NimBLE, advertises, scans, opens Wi-Fi, or calls pairing crypto from GATT. */
#ifndef HB_MUSE_PAIRING_GATT_H
#define HB_MUSE_PAIRING_GATT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  /* Must gate real active connection + subscription + local setup window. */
  bool (*current)(void *user, uint16_t connection);
  /* Must COPY this <=512-byte fragment into a bounded, nonblocking queue,
   * tagged with a unique connection/window generation. Full queue => false,
   * invalidate partial reassembly. Worker calls codec then pairing_receive.
   * Do not invoke crypto, parser, NVS or network synchronously here. */
  bool (*defer_rx)(void *user, uint16_t connection, const uint8_t *data, size_t bytes);
  void *user;
} hb_muse_pairing_gatt_config_t;
/* Once, after existing NimBLE init but BEFORE existing host starts/services
 * register. Failure is latched; reboot/rebuild registration, no second owner.
 * This does not advertise. Existing owner must explicitly register this UUID in
 * its chosen exclusive setup mode and route GAP lifecycle/subscription events. */
bool hb_muse_pairing_gatt_register(const hb_muse_pairing_gatt_config_t *config);
bool hb_muse_pairing_gatt_registered(void);
uint16_t hb_muse_pairing_gatt_tx_handle(void);
/* Existing exclusive BT owner only, AFTER actual host stop/deinit. Never call
 * with active GATT callbacks; invalidates cached handles before re-registration. */
void hb_muse_pairing_gatt_stack_reset(void);
/* Worker only, one already-fragmented <=160-byte notification. The caller must
 * check SDK record/session generations before EVERY fragment. No retries here. */
bool hb_muse_pairing_gatt_notify(uint16_t connection, const uint8_t *fragment, size_t bytes);
#ifdef __cplusplus
}
#endif

#endif
