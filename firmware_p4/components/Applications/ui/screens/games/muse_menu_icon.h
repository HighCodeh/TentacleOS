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

#ifndef FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_MUSE_MENU_ICON_H
#define FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_MUSE_MENU_ICON_H
#include "lvgl.h"
#include "muse_app_icon.inc"
// Avoid scaled I4 in this LVGL version's partial-line decoder. Expand once
// into bounded PSRAM without changing the decoder, radio DMA heap or SD.
#ifdef __cplusplus
extern "C" {
#endif

EXT_RAM_BSS_ATTR static uint32_t s_muse_logo_rgba[64 * 64];
static const lv_image_dsc_t *muse_menu_icon(void) {
  static bool ready;
  static const lv_image_dsc_t rgba = {.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                                 .cf = LV_COLOR_FORMAT_ARGB8888,
                                                 .w = 64,
                                                 .h = 64,
                                                 .stride = 256},
                                      .data_size = sizeof s_muse_logo_rgba,
                                      .data = (const uint8_t *)s_muse_logo_rgba};
  if (!ready) {
    for (unsigned i = 0; i < 64 * 64; ++i) {
      uint8_t packed = s_muse_logo.data[64 + i / 2];
      unsigned index = (i & 1) ? (packed & 15) : (packed >> 4);
      const uint8_t *p = s_muse_logo.data + index * 4;
      s_muse_logo_rgba[i] =
          (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    ready = true;
  }
  return &rgba;
}

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_P4_COMPONENTS_APPLICATIONS_UI_SCREENS_GAMES_MUSE_MENU_ICON_H
