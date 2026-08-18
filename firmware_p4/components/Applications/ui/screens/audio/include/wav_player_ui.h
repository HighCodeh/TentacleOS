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

#ifndef WAV_PLAYER_UI_H
#define WAV_PLAYER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the .wav file path to play. Call this BEFORE switching to
 *        SCREEN_WAV_PLAYER (e.g. from the Files screen). The string is copied.
 *        Resets the player to standalone (no playlist) mode: on finish or skip
 *        the same track repeats. The Player library overrides this by also
 *        calling ui_wav_player_set_index().
 */
void ui_wav_player_set_path(const char *path);

/**
 * @brief Bind the player to the Player-library track list at index @p index,
 *        enabling prev/next navigation and auto-advance. Call AFTER
 *        ui_wav_player_set_path() and BEFORE switching to SCREEN_WAV_PLAYER.
 */
void ui_wav_player_set_index(int index);

/**
 * @brief Set the screen to return to on BACK (as a screen_id_t value). Defaults
 *        to the Files screen. The Player library sets it to itself so BACK from
 *        a track returns to the track list.
 */
void ui_wav_player_set_return(int screen);

/** @brief Open the WAV player screen and start streaming the set path. */
void ui_wav_player_open(void);

/**
 * @brief Stop playback and release the I2S channel, waiting for the decode task
 *        to exit. Registered as the screen close hook so navigating away (or
 *        handing off to the MP3 player) tears down cleanly with no audio clash.
 */
void ui_wav_player_stop(void);

#ifdef __cplusplus
}
#endif

#endif // WAV_PLAYER_UI_H
