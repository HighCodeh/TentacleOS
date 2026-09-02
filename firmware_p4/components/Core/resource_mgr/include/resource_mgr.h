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

// Arbitration for the shared radios and peripherals. Apps (via the ABI), the
// on-device UI, the companion (host_link), and the firmware core all acquire a
// resource here before driving the hardware, so they never fight over one radio
// while still allowing the concurrency the hardware supports (BLE multiplexes
// into connection/advertising/scanner lanes).
//
// This component depends only on FreeRTOS: enforcement of a preemption is done
// through the owner's on_revoke callback, not by calling any transport here, so
// both the Service and Applications components can require it without a cycle.

#ifndef RESOURCE_MGR_H
#define RESOURCE_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** The physical resources apps and the OS contend for. */
typedef enum {
  RES_WIFI = 0, ///< one attack/mode at a time on the C5 WiFi radio
  RES_BLE,      ///< exclusive: the C5 serializes the whole BLE host (attacks tear down GATT)
  RES_SUBGHZ,   ///< CC1101, P4-local
  RES_IR,       ///< RMT, P4-local
  RES_NFC,
  RES_HID_USB, ///< BadUSB / HID injection
  RES_LORA,
  RES_DISPLAY, ///< the ST7789 panel: one owner draws the screen at a time (UI or an app)
  RES_COUNT
} res_id_t;

/** Lane index within a resource. Every resource here is exclusive: one lane. */
enum {
  RES_LANE_MAIN = 0,
};

/** Who is asking. Determines default priority and reservation privilege. */
typedef enum {
  RES_OWNER_SYSTEM = 0, ///< firmware core (connectivity, OTA); highest
  RES_OWNER_COMPANION,  ///< the host_link control channel
  RES_OWNER_UI,         ///< on-device attack screen
  RES_OWNER_APP,        ///< a loaded app
  RES_OWNER_COUNT
} res_owner_kind_t;

typedef uint32_t res_handle_t;
#define RES_HANDLE_NONE 0u

/** One acquisition request. */
typedef struct {
  res_id_t id;
  uint8_t lane;                  ///< lane within the resource (RES_LANE_MAIN for exclusive)
  res_owner_kind_t owner_kind;
  void *owner_task;              ///< APP: its TaskHandle_t; else NULL
  uint8_t priority;              ///< 0 -> default for the owner kind
  bool allow_preempt;            ///< may preempt a strictly lower-priority holder
  void (*on_revoke)(void *user); ///< called (outside the lock) if this grant is preempted or bulk-released
  void *user;
} res_request_t;

/** @brief Initialize the manager. Safe to call more than once. */
esp_err_t resource_mgr_init(void);

/**
 * @brief Acquire one slot of a resource lane.
 * @return ESP_OK and *out_handle set on success; ESP_ERR_INVALID_STATE if an
 *         exclusive lane is held by an owner of equal-or-higher priority;
 *         ESP_ERR_NO_MEM if a slotted lane has no free slot; ESP_ERR_INVALID_ARG
 *         on a bad request. On a successful preemption the previous holder's
 *         on_revoke has already fired.
 */
esp_err_t resource_acquire(const res_request_t *req, res_handle_t *out_handle);

/**
 * @brief Like resource_acquire, but retry for up to @p timeout_ms before giving
 *        up with the same error resource_acquire would return. Blocks the caller
 *        (polls), so never call it from a time-critical context. @p timeout_ms 0
 *        behaves exactly like resource_acquire (one try, fail-fast).
 */
esp_err_t resource_acquire_timeout(const res_request_t *req, uint32_t timeout_ms,
                                   res_handle_t *out_handle);

/** @brief Release a grant. Idempotent; a stale handle is ignored. */
esp_err_t resource_release(res_handle_t handle);

/** @brief True if @p handle still names a live grant (not released or preempted). */
bool resource_handle_valid(res_handle_t handle);

/** @brief True if the lane has a slot free for @p owner_kind right now. */
bool resource_available(res_id_t id, uint8_t lane, res_owner_kind_t owner_kind);

/** @brief Release every grant held by an app task (call on app exit). Fires each on_revoke. */
void resource_release_owner_task(void *owner_task);

/** @brief Release every grant held by an owner kind (call on companion disconnect). */
void resource_release_owner_kind(res_owner_kind_t kind);

/** One lane's live state, for introspection. */
typedef struct {
  res_id_t id;
  uint8_t lane;
  const char *res_name;
  const char *lane_name;
  uint8_t capacity;
  uint8_t reserved;
  uint8_t used;
  res_owner_kind_t holder; ///< kind of one current holder, or RES_OWNER_COUNT if none
} res_status_t;

/** @brief Fill @p out with up to @p max lane states. Returns the number written. */
int resource_snapshot(res_status_t *out, int max);

/** @brief Human string for an owner kind (for logs / the console). */
const char *resource_owner_name(res_owner_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif // RESOURCE_MGR_H
