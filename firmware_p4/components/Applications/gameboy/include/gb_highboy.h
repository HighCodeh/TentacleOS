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

#ifndef GB_HIGHBOY_H
#define GB_HIGHBOY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Launch the Game Boy (DMG) emulator (Peanut-GB core) in its own task.
 *
 * The emulator takes over the ST7789 (landscape, stretched to full screen) and
 * runs until the user holds OK+BACK for 2 s with the D-pad released.
 * OK=A, BACK=B, both=A+B.
 * Physical RIGHT+both=START; LEFT+both=SELECT.
 * Battery-backed cartridge RAM is
 * persisted to
 * <rom>.sav next to the ROM on the SD card.
 *
 * On exit the task tears itself down (stops audio, frees its buffers, releases
 * the LVGL lock) and reports highboy_gb_finished(); the ROM picker then restores
 * the panel orientation and returns to the games menu without a firmware reboot.
 *
 * @param rompath  Full SD path of the .gb/.gbc ROM to run, or NULL/"" to
 *                 auto-discover the first ROM on the SD card.
 */
void highboy_gb_start(const char *rompath);

/**
 * @brief Whether the emulator task has fully torn down.
 *
 * Polled by the ROM picker's navigation timer (LVGL task) so it can restore the
 * panel and switch to the games menu once the emulator has released the panel.
 * Reset to false by the next highboy_gb_start().
 *
 * @return true once teardown is complete, false while the emulator is running.
 */
bool highboy_gb_finished(void);

/** Last launch/runtime error, or an empty string. Read after finished() is true. */
const char *highboy_gb_last_error(void);

/**
 * Validate the selected file's header and size without allocating its ROM.
 * Does not mount
 * the SD card or prove full gameplay compatibility.
 * Writes a short error to reason when
 * provided; clears it on success.
 */
bool highboy_gb_validate_rom(const char *rompath, char *reason, size_t reason_size);

#ifdef __cplusplus
}
#endif

#endif // GB_HIGHBOY_H
