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

#ifndef FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_INCLUDE_GAME_COVER_H
#define FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_INCLUDE_GAME_COVER_H
#include <stdbool.h>
#include "lvgl.h"

// HBC1 + LE uint16 width/height + exact LE RGB565 pixels, max 160x144.
// One shared PSRAM buffer; no decoder or embedded artwork.
lv_obj_t *game_cover_create(lv_obj_t *parent);
#ifdef __cplusplus
extern "C" {
#endif

bool game_cover_show(lv_obj_t *image, const char *key, int box_w, int box_h);
bool game_cover_show_rom(lv_obj_t *image, const char *rom_path, int box_w, int box_h);
void game_cover_release(void);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_INCLUDE_GAME_COVER_H
