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

#ifndef WAV_LIBRARY_UI_H
#define WAV_LIBRARY_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** @brief Open the audio player library: lists every .wav / .mp3 on the SD card. */
void ui_wav_library_open(void);

/** @brief True if track @p i is an .mp3 (else it is a .wav). */
bool ui_wav_library_is_mp3(int i);

/**
 * @brief Play library track @p i in the correct player (wav or mp3), wrapping
 *        the index. Used by both players for prev/next and auto-advance so
 *        navigation flows seamlessly across a mixed wav+mp3 list. @p return_screen
 *        is a screen_id_t value the opened player returns to on BACK.
 */
void ui_player_play_index(int i, int return_screen);

/** @brief Number of .wav tracks found on the last scan. */
int ui_wav_library_count(void);

/** @brief Full path of track @p i, or NULL if out of range. */
const char *ui_wav_library_path(int i);

/** @brief File name of track @p i, or NULL if out of range. */
const char *ui_wav_library_name(int i);

/** @brief Move the highlighted row so BACK returns to the playing track. */
void ui_wav_library_set_selected(int i);

#ifdef __cplusplus
}
#endif

#endif // WAV_LIBRARY_UI_H
