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

#ifndef MSGBOX_UI_H
#define MSGBOX_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef void (*msgbox_cb_t)(bool confirm);

/** @brief Open a message box with optional OK and Cancel buttons. */
void msgbox_open(
    const char *icon, const char *msg, const char *btn_ok, const char *btn_cancel, msgbox_cb_t cb);

/**
 * @brief Open the "SD card connected" info modal (badge + card details + OK).
 *
 * A centered card showing the SD icon in an accent badge with a check mark,
 * a title, and a Size/Free/Type list. Dismisses on OK or Back. Reuses the
 * message-box input machinery, so msgbox_is_open() reports it too.
 *
 * @param name  Card label/name (may be NULL).
 * @param size  Total capacity string, e.g. "32 GB".
 * @param free  Free space string, e.g. "29.7 GB".
 * @param fmt   Filesystem label, e.g. "FAT32".
 */
void msgbox_open_sd_info(const char *name, const char *size, const char *free, const char *fmt);

/**
 * @brief Open a centered info/result modal (icon badge + title + message + OK).
 *
 * Same look and input handling as the SD info modal: a centered card that
 * slides up, an accent-tinted icon badge, an accent title, a wrapped body
 * message and a single OK button. Dismisses on OK or Back.
 *
 * @param icon_path  ".bin" asset path for the badge icon (NULL to omit).
 * @param title      Accent title text.
 * @param msg        Body message (may wrap over several lines).
 * @param accent     Accent color for the badge/title/border.
 */
void msgbox_open_info(const char *icon_path, const char *title, const char *msg, lv_color_t accent);

/** @brief Close the currently open message box. */
void msgbox_close(void);

/** @brief Check if a message box is currently open. */
bool msgbox_is_open(void);

#ifdef __cplusplus
}
#endif

#endif // MSGBOX_UI_H