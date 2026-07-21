// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0

#ifndef LVGL_GLUE_H
#define LVGL_GLUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief Stand up LVGL on top of the existing ST7789 panel.
 *
 * Wires the already-initialized esp_lcd panel/IO handles into LVGL's
 * display driver via esp_lvgl_port, spawns the LVGL tick + handler task,
 * and returns a usable lv_display_t pointer.
 *
 * Safe to call multiple times — second call is a no-op and returns the
 * existing display. Idempotent so test screens that want LVGL can call
 * this without worrying about who initialized it first.
 *
 * @return ESP_OK on success.
 */
esp_err_t lvgl_glue_init(void);

/**
 * @brief Returns true if lvgl_glue_init() has run successfully.
 *
 * Test screens should check this before calling LVGL APIs — if LVGL didn't
 * come up (e.g. PSRAM short, alloc failure), they should fall back to the
 * raw display.c rendering path.
 *
 * @return true if LVGL is initialized and ready.
 */
bool lvgl_glue_is_ready(void);

/**
 * @brief Acquire the LVGL mutex.
 *
 * Any task that calls into LVGL outside the LVGL handler task itself
 * (e.g. a test screen that pushes UI updates from a button callback)
 * MUST wrap those calls with lock/unlock — LVGL itself isn't thread-safe.
 *
 * @param timeout_ms  Milliseconds to wait for the lock; <0 means wait forever.
 * @return true if the lock was acquired, false on timeout.
 */
bool lvgl_glue_lock(int timeout_ms);

/**
 * @brief Release the LVGL mutex previously taken with lvgl_glue_lock().
 */
void lvgl_glue_unlock(void);

/**
 * @brief Toggle between portrait (LV_DISPLAY_ROTATION_0) and landscape-
 *        left (LV_DISPLAY_ROTATION_90).
 *
 * Triggers a full framebuffer invalidation so the next LVGL frame
 * re-renders into the new resolution.
 *
 * @return true if we ended up in landscape.
 */
bool lvgl_glue_toggle_rotation(void);

/**
 * @brief Returns true if the display is currently in landscape-left mode.
 *        Used by the keypad indev to decide how to remap button → key.
 *
 * @return true if in landscape-left mode.
 */
bool lvgl_glue_is_landscape(void);

#ifdef __cplusplus
}
#endif

#endif // LVGL_GLUE_H
