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

#ifndef BOOT_SOUND_H
#define BOOT_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Play an MP3 file once on a dedicated task.
 *
 * Decodes a bounded @p path with Helix into owned PSRAM, then plays via I2S
 * without blocking the caller. Intended for the boot sound: call it after
 * @c audio_i2s_init() and after the filesystem holding @p path is mounted.
 * Only one boot sound runs at a time; a second call while one is playing is
 * rejected. Files up to 256 KiB and 10 seconds of mono/stereo audio are accepted.
 * A 5-second decode budget and internal stack bound malformed-input work.
 * Missing files, low heap and playback errors skip this optional sound.
 * The task frees all resources on every exit.
 *
 * @param path  Absolute VFS path to the .mp3, e.g. "/assets/storage/ghost_cyberpunk_boot.mp3".
 * @return
 *   - ESP_OK if the playback task was started
 *   - ESP_ERR_INVALID_ARG if @p path is NULL, relative or too long
 *   - ESP_ERR_INVALID_STATE if a boot sound is already playing
 *   - ESP_ERR_NO_MEM if internal heap is low or the task could not be created
 */
esp_err_t boot_sound_play(const char *path);

#ifdef __cplusplus
}
#endif

#endif // BOOT_SOUND_H
