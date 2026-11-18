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

#ifndef APP_SWITCHER_UI_H
#define APP_SWITCHER_UI_H
#include "ui_manager.h"

/** Session-only native task cards. All calls run with the LVGL lock held. */
bool ui_tasks_is_open(void);
bool ui_tasks_is_resuming(void);
void ui_tasks_toggle(void);
void ui_tasks_close(void);
void ui_tasks_reset(void);
void ui_tasks_input(const input_event_t *event);
void ui_tasks_before_switch(screen_id_t from, screen_id_t to);
void ui_tasks_after_switch(screen_id_t to);
#endif
