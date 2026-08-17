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

#ifndef LVGL_SCREEN_SHARE_H
#define LVGL_SCREEN_SHARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Screen sharing over the USB host link. The device snapshots the live LVGL
// screen (lv_snapshot) and streams it as RGB565 row-strips to the companion
// app; the app's control buttons come back as key injections into the keypad.
//
// Commands (SPI_CAT_SCREEN) are dispatched locally by host_link via the two
// functions below - they are never relayed to the C5.

/**
 * @brief Whether a host-link command id belongs to screen sharing.
 * @param cmd  SPI_CMD(category, op) id.
 */
bool lvgl_screen_share_is_host_op(uint16_t cmd);

/**
 * @brief Handle a screen-share command from the companion app.
 *
 * START/STOP toggle the capture task; KEY injects a control key. Any response
 * bytes are written to out (normally none).
 *
 * @return an SPI_STATUS_* code.
 */
uint8_t lvgl_screen_share_handle(uint16_t cmd,
                                 const uint8_t *payload,
                                 uint16_t plen,
                                 uint8_t *out,
                                 size_t out_cap,
                                 uint16_t *out_len);

/**
 * @brief Stop streaming and free the capture buffer.
 *
 * Idempotent. Called on an explicit STOP and, as a safeguard, whenever the
 * host-link session drops (USB unplug / app disconnect) so the P4 never stays
 * capturing into a dead link.
 */
void lvgl_screen_share_stop(void);

/** @brief True while the capture/stream loop is running. */
bool lvgl_screen_share_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // LVGL_SCREEN_SHARE_H
