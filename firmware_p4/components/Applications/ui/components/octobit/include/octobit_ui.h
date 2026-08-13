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

#ifndef OCTOBIT_UI_H
#define OCTOBIT_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Show the Octobit mascot snug in the bottom-right corner with a speech
 *        balloon. Use for status states ("Pairing...", "Searching...", etc.).
 *
 * @param parent  Screen/container to attach to.
 * @param phrase  Text shown in the balloon (may be NULL for none).
 * @return The root object; pass it to octobit_set_text() / lv_obj_del().
 */
lv_obj_t *octobit_create(lv_obj_t *parent, const char *phrase);

/** @brief Update the balloon phrase of an existing Octobit. */
void octobit_set_text(lv_obj_t *octobit_root, const char *phrase);

#ifdef __cplusplus
}
#endif

#endif // OCTOBIT_UI_H
