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

#ifndef MENU_UI_H
#define MENU_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ui_manager.h"
#include "lvgl.h"

/** @brief Open the main menu screen. */
void ui_menu_open(void);

/** @brief Number of apps in the menu catalog. */
int menu_catalog_count(void);

/** @brief App name at @p index, or NULL if out of range. */
const char *menu_catalog_name(int index);

/** @brief Target screen of the app at @p index (SCREEN_HOME if out of range). */
screen_id_t menu_catalog_target(int index);

/** Read-only render of this menu's real catalog/assets/carousel, never a screen
 * switch or a
 * second firmware instance. UI/LVGL task only; no input, radio,
 * favorites writes, global header
 * binding, or persistent animation timers. */
typedef struct menu_ui_preview menu_ui_preview_t;
menu_ui_preview_t *menu_ui_preview_create(void);
bool menu_ui_preview_select(menu_ui_preview_t *preview, unsigned index);
lv_obj_t *menu_ui_preview_root(menu_ui_preview_t *preview);
void menu_ui_preview_destroy(menu_ui_preview_t *preview);

#ifdef __cplusplus
}
#endif

#endif // MENU_UI_H
