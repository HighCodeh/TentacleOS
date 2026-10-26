/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/*
 * Two-button menu for boards without touch, in place of the settings tile.
 * The aux button opens it and then steps down the list; the talk button
 * selects. Hints above the buttons say what each one does.
 */

typedef enum {
    MUSE_MENU_DOWN,     /* aux button: opens the menu, then moves down */
    MUSE_MENU_SELECT,   /* talk button, while the menu is open */
} muse_menu_key_t;

/* Safe from any task. */
void muse_menu_key(muse_menu_key_t key);
bool muse_menu_is_open(void);

/* LVGL task only. */
void muse_menu_build(lv_obj_t *parent, int w, int h);
/* Handles queued keys; returns true while the menu covers the face. */
bool muse_menu_tick(float now);
void muse_menu_close(void);
