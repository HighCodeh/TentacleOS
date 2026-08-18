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

#ifndef UI_THEME_H
#define UI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "lvgl.h"

/** @brief Theme color configuration for the UI. */
typedef struct {
  lv_color_t bg_primary;
  lv_color_t bg_secondary;
  lv_color_t bg_item_top;
  lv_color_t bg_item_bot;
  lv_color_t border_accent;
  lv_color_t border_interface;
  lv_color_t border_inactive;
  lv_color_t text_main;
  lv_color_t text_secondary;
  lv_color_t screen_base;

  lv_color_t protocol_nfc;
  lv_color_t protocol_wifi;
  lv_color_t protocol_ble;
  lv_color_t protocol_subghz;
  lv_color_t protocol_rfid;
  lv_color_t protocol_ir;
  lv_color_t protocol_lora;
} ui_theme_t;

/** @brief Protocol identifiers for accent color selection. */
typedef enum {
  PROTOCOL_NONE,
  PROTOCOL_NFC,
  PROTOCOL_WIFI,
  PROTOCOL_BLE,
  PROTOCOL_SUBGHZ,
  PROTOCOL_RFID,
  PROTOCOL_IR,
  PROTOCOL_LORA,
  PROTOCOL_COUNT
} protocol_id_t;

extern ui_theme_t current_theme;

/** @brief Initialize the theme system from config or fallback. */
void ui_theme_init(void);

/** @brief Load a built-in theme by index. */
void ui_theme_load_idx(int color_idx);

/** @brief Load a theme by name from SD or flash. */
void ui_theme_load_from_name(const char *theme_name);

/** @brief Load theme from the stored config file. */
void ui_theme_load_from_sd(void);

/** @brief Set the active protocol for accent color. */
void ui_theme_set_protocol(protocol_id_t protocol);

/** @brief Get the accent color for the active protocol. */
lv_color_t ui_theme_get_accent(void);

#ifdef __cplusplus
}
#endif

#endif // UI_THEME_H