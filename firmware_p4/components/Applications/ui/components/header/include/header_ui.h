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

#ifndef UI_HEADER_H
#define UI_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "lvgl.h"

/** @brief Create the header bar on the given parent. */
void header_ui_create(lv_obj_t *parent);

/**
 * @brief Attach the shared status cluster (wifi/bt/sd/battery) to @p parent.
 *
 * Builds the exact same icons + starts the exact same singleton status timers
 * the home/menu header uses, right-aligned inside @p parent. Reused by the
 * per-screen chrome header so every screen shows the identical status bar with
 * identical behavior (charging animation, SD mount, BLE tint, ...).
 *
 * @param parent    Bar object to attach the cluster to.
 * @param y_offset  Vertical nudge to the parent's visual center (home's bar is
 *                  drawn with a negative top inset -> 6; the chrome bar -> 0).
 */
void header_ui_attach_status(lv_obj_t *parent, int y_offset);

/**
 * @brief Static one-shot version of the status cluster (no globals, no timers).
 *
 * Paints wifi/bt/sd/battery at the CURRENT state but does not bind the shared
 * statics nor register the animation timers, so it never dangles when torn down.
 * For transient overlays drawn over a live screen. See header_ui_create_snapshot.
 */
void header_ui_attach_status_snapshot(lv_obj_t *parent, int y_offset);

/**
 * @brief Full home-style status bar, but STATIC (snapshot cluster, no timers).
 *
 * Same visual as header_ui_create; use on a transient overlay so it does not
 * rebind/dangle the dynamic header of the screen underneath.
 */
void header_ui_create_snapshot(lv_obj_t *parent);

/**
 * @brief Animate the WiFi icon while a connection is in progress.
 *
 * Call with true when a connect attempt starts and false when it finishes
 * (connected or failed). While false the icon is static and reflects the real
 * WiFi state; there is no perpetual animation. Safe to call from the UI thread.
 */
void header_ui_set_wifi_connecting(bool connecting);

/**
 * @brief Report the cached SD card usage, computed off the LVGL task.
 *
 * Reads a value cached by the header's mount worker, so callers (e.g. the
 * home dropdown) avoid a blocking filesystem query on the LVGL task.
 *
 * @param[out] out_used_pct  Used space percentage (0-100); may be NULL.
 * @return true if a card is currently mounted/recognized.
 */
bool header_ui_sd_usage(int *out_used_pct);

/**
 * @brief Request the SD card mount owner (the card-detect worker) to remount.
 *
 * Routes a remount through the single task that owns the SD mount, so a health
 * check failing elsewhere (e.g. the system monitor) never remounts concurrently
 * with the hotplug worker. Safe to call from any task; no-op if the worker is
 * not up yet.
 */
void header_ui_request_sd_remount(void);

#ifdef __cplusplus
}
#endif

#endif // UI_HEADER_H
