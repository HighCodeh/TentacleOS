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

#ifndef DOOM_HIGHBOY_H
#define DOOM_HIGHBOY_H

#ifdef __cplusplus
extern "C" {
#endif

// Launch official DOOM (doomgeneric) in its own FreeRTOS task. Takes over the
// ST7789 (landscape, bypassing LVGL) and streams /sdcard/doom1.wad. Call from
// the games-menu screen open handler. Quitting DOOM reboots the device
// (hold OK+BACK ~2s), so this does not return control to the caller's UI.
//
// render_beat_kick: optional callback pulsed once per frame while DOOM owns the
// panel (LVGL is parked). Pass ui_render_beat_kick so the render-liveness
// watchdog (sys_monitor) sees progress; NULL disables it.
void highboy_doom_start(void (*render_beat_kick)(void));

#ifdef __cplusplus
}
#endif

#endif  // DOOM_HIGHBOY_H
