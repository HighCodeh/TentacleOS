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

#ifndef MP4_PLAYER_UI_H
#define MP4_PLAYER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the .mp4 / .m4v / .mov file to open. Call BEFORE switching to
 *        SCREEN_MP4_PLAYER (e.g. from the Files screen). The string is copied.
 */
void ui_mp4_player_set_path(const char *path);

/**
 * @brief Set the screen to return to on BACK (as a screen_id_t value).
 *        Defaults to the Files screen.
 */
void ui_mp4_player_set_return(int screen);

/** @brief Open the MP4 player screen: parses metadata + cover and shows them. */
void ui_mp4_player_open(void);

#ifdef __cplusplus
}
#endif

#endif // MP4_PLAYER_UI_H
