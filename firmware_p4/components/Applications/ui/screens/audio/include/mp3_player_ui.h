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

#ifndef MP3_PLAYER_UI_H
#define MP3_PLAYER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the .mp3 file to play. Call BEFORE switching to SCREEN_MP3_PLAYER
 *        (e.g. from the Files screen). The string is copied.
 */
void ui_mp3_player_set_path(const char *path);

/** @brief Set the screen to return to on BACK (as a screen_id_t value). */
void ui_mp3_player_set_return(int screen);

/**
 * @brief Bind to the Player library at index @p index (playlist mode) so
 *        prev/next and auto-advance navigate the shared wav+mp3 list. Call AFTER
 *        ui_mp3_player_set_path() (which resets to standalone) and before opening.
 */
void ui_mp3_player_set_index(int index);

/** @brief Open the MP3 player: parses ID3 + cover, shows them, starts playback. */
void ui_mp3_player_open(void);

/**
 * @brief Stop playback and release the I2S channel, waiting for the decode task
 *        to exit. Registered as the screen close hook for clean navigation/handoff.
 */
void ui_mp3_player_stop(void);

#ifdef __cplusplus
}
#endif

#endif // MP3_PLAYER_UI_H
