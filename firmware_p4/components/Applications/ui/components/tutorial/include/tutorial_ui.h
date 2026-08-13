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

#ifndef TUTORIAL_UI_H
#define TUTORIAL_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief First-boot onboarding wizard: a full-screen sequence of pages
 *        (setup + welcome) driven with OK (next) / BACK (previous). On the last
 *        page OK marks it done in NVS, closes the overlay and loads the home
 *        screen. It hijacks the input group while up.
 *
 * Call once at boot, right after the home screen is opened, guarded by
 * tutorial_should_run().
 */
void tutorial_start(void);

/** @brief True on the very first boot (the "done" NVS flag is not set yet). */
bool tutorial_should_run(void);

/** @brief True while the onboarding wizard is on screen and holding the input. */
bool tutorial_is_active(void);

/** @brief Clear the "done" flag so the wizard runs again on next boot/start. */
void tutorial_reset(void);

#ifdef __cplusplus
}
#endif

#endif // TUTORIAL_UI_H
