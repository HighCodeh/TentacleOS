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

// Per-app execution context and capability enforcement. Firmware-internal: this
// is NOT part of the app-facing ABI and is never shipped in the SDK.
//
// An app runs in its own task; the app manager binds that task to the app's
// context so a call through tos_api can find who is calling and check its grant.
// A call from a task with no bound context is treated as the trusted firmware
// itself and passes every capability check.

#ifndef TOS_APP_CTX_H
#define TOS_APP_CTX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define TOS_MAX_APP_REGS 16

/** One command the app registered, tracked so the manager can undo it on exit. */
typedef struct {
  uint8_t kind; ///< 0 = console, 1 = host_link
  char console_cmd[24];
  uint8_t hl_category;
  uint8_t hl_op_min;
} tos_app_reg_t;

typedef struct {
  tos_app_reg_t items[TOS_MAX_APP_REGS];
  int count;
} tos_app_regs_t;

/** One running app's execution context. */
typedef struct {
  uint32_t granted_caps;    ///< bitmask of tos_cap_t the loader granted
  const char *id;           ///< app id, for logs
  volatile bool *stop;      ///< set true to ask the app to exit (NULL = never)
  volatile bool *res_lost;  ///< set true when a radio the app held was preempted (NULL = untracked)
  struct tos_arena *arena;  ///< per-app allocation arena (NULL = untracked)
  tos_app_regs_t *regs;     ///< commands the app registered (NULL = untracked)
} tos_app_ctx_t;

/** @brief Set the resource-lost flag for the app bound to @p task (revoke side). */
void tos_app_ctx_signal_resource_lost(void *task);

/** @brief Clear the calling app's resource-lost flag (on a successful acquire). */
void tos_app_ctx_clear_resource_lost(void);

/** @brief Record a console/host_link command the running app registered. */
void tos_app_regs_record_console(tos_app_regs_t *regs, const char *cmd);
void tos_app_regs_record_hostlink(tos_app_regs_t *regs, uint8_t category, uint8_t op_min);

/** @brief Unregister everything the app registered (call on app exit). */
void tos_app_regs_teardown(tos_app_regs_t *regs);

/** @brief Bind a FreeRTOS task (TaskHandle_t) to an app context. */
esp_err_t tos_app_ctx_bind(void *task, tos_app_ctx_t *ctx);

/** @brief Remove the binding for a task. */
void tos_app_ctx_unbind(void *task);

/** @brief Context bound to the calling task, or NULL if the caller is firmware. */
tos_app_ctx_t *tos_app_ctx_current(void);

/**
 * @brief True if the caller may use a capability.
 * @return true for the trusted firmware (no bound context) or when the calling
 *         app was granted every bit in @p cap; false otherwise.
 */
bool tos_app_cap_check(uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif // TOS_APP_CTX_H
