// SPDX-License-Identifier: GPL-3.0-or-later
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
