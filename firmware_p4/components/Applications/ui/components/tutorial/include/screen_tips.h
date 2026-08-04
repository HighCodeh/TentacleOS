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

#ifndef SCREEN_TIPS_H
#define SCREEN_TIPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "ui_manager.h"

/**
 * @brief First-view screen tip. Call right after a screen is loaded and added
 *        to the input group. The first time a screen with a tip is opened,
 *        Octobit's explanation pops up over it (the screen behind dims and
 *        locks; OK/BACK dismisses). Seen screens are tracked in NVS and never
 *        nag again. No-op if the screen has no tip, was already seen, or a tip
 *        is already up.
 */
void screen_tips_hook(screen_id_t screen);

/** @brief True while a screen tip is on screen and holding the input. */
bool screen_tips_active(void);

/** @brief Clear every seen flag so all first-view tips play again. */
void screen_tips_reset(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_TIPS_H
