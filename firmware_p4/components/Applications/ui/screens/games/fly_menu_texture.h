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

#ifndef FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_FLY_MENU_TEXTURE_H
#define FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_FLY_MENU_TEXTURE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLY_MENU_TEXTURE_WIDTH       96U
#define FLY_MENU_TEXTURE_HEIGHT      128U
#define FLY_MENU_TEXTURE_PIXELS      (FLY_MENU_TEXTURE_WIDTH * FLY_MENU_TEXTURE_HEIGHT)
#define FLY_MENU_TEXTURE_INTERVAL_MS 1800U

// UI/LVGL task only. This renders the real menu/catalog as a read-only demo
// loop, NOT independent firmware execution and NOT remote control of radios.
// App-owned buffers/widgets are allocated once at open; LVGL's own renderer
// may allocate internal draw tasks/decoder scratch during snapshots.
#ifdef __cplusplus
extern "C" {
#endif

bool fly_menu_texture_open(void);
bool fly_menu_texture_tick(uint32_t elapsed_ms);
const uint16_t *fly_menu_texture_pixels(void); // NULL => use a black screen.
void fly_menu_texture_close(void);

// Bounded pure helper, also used by host tests. Native-endian RGB565.
bool fly_menu_texture_downsample(const uint16_t *source,
                                 size_t source_capacity,
                                 unsigned source_width,
                                 unsigned source_height,
                                 unsigned source_stride,
                                 uint16_t *destination,
                                 size_t destination_capacity);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_FLY_MENU_TEXTURE_H
