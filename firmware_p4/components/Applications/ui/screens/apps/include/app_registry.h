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

// Shared backend for the on-device app launchers: enumerates signed .hb bundles in
// /sdcard/apps (title + icon, metadata only) and launches one by identity name
// (verify + capability consent + start). Used by both the Apps carousel (shown next
// to the built-in games) and the DEV -> Apps list, so each renders SD apps the same
// way without duplicating the .hb format/consent glue.

#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#define APP_REG_MAX 20

/** One installed .hb app, for a launcher UI. */
typedef struct {
  char file[32];       ///< base name without ".hb" — launch identity + grants key
  char title[64];      ///< embedded display title, or "" (caller falls back to file)
  bool has_icon;       ///< true if @ref icon is a valid embedded icon
  lv_image_dsc_t icon; ///< embedded icon (valid iff has_icon); pixels are @ref icon_px
  uint8_t *icon_px;    ///< owned icon pixels; released by app_registry_free()
} app_reg_entry_t;

/**
 * @brief Scan /sdcard/apps for signed .hb bundles (metadata only, no signature
 *        verify — display use; launch re-verifies).
 *
 * Fills up to @p max caller-owned entries, decoding an icon for each app that
 * embeds one. Returns the count. Always pair a scan with app_registry_free()
 * before rescanning or discarding @p out, to release the icon pixel buffers.
 */
int app_registry_scan(app_reg_entry_t *out, int max);

/** @brief Release the icon pixel buffers of a scanned list. Safe on a zero count. */
void app_registry_free(app_reg_entry_t *list, int count);

/**
 * @brief Launch a .hb by identity name: read, verify the signature, prompt for
 *        capability consent on first run, then start it. Shows a msgbox on
 *        error/consent. Callable from any screen.
 */
void app_registry_launch(const char *file);

#ifdef __cplusplus
}
#endif

#endif // APP_REGISTRY_H
