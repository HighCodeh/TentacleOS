// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SPINNER_UI_H
#define SPINNER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef struct {
    lv_obj_t  *obj;
    lv_timer_t *timer;
} spinner_ui_t;

/**
 * @brief Create a ring spinner (arc rotating).
 *
 * @param parent  Parent LVGL object.
 * @param size    Diameter in pixels.
 * @return spinner handle.
 */
spinner_ui_t spinner_ui_create(lv_obj_t *parent, int32_t size);

void spinner_ui_show(spinner_ui_t *s);
void spinner_ui_hide(spinner_ui_t *s);
void spinner_ui_delete(spinner_ui_t *s);

#ifdef __cplusplus
}
#endif

#endif // SPINNER_UI_H
