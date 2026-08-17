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

#include "footer_ui.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"

#include "cJSON.h"
#include "storage_assets.h"
#include "tos_flash_paths.h"
#include "ui_theme.h"

#define FOOTER_HEIGHT         20
#define INTERFACE_CONFIG_PATH FLASH_CONFIG_INTERFACE

static bool footer_is_hidden(void) {
  bool hidden = false;
  if (!storage_assets_is_mounted())
    return hidden;
  FILE *f = fopen(INTERFACE_CONFIG_PATH, "r");
  if (f) {
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize + 1);
    if (data) {
      fread(data, 1, fsize, f);
      data[fsize] = 0;
      cJSON *root = cJSON_Parse(data);
      if (root) {
        cJSON *f_sw = cJSON_GetObjectItem(root, "hide_footer");
        if (cJSON_IsBool(f_sw))
          hidden = cJSON_IsTrue(f_sw);
        cJSON_Delete(root);
      }
      free(data);
    }
    fclose(f);
  }
  return hidden;
}

void footer_ui_create(lv_obj_t *parent) {
  if (footer_is_hidden())
    return;

  lv_obj_t *footer = lv_obj_create(parent);
  lv_obj_set_size(footer, lv_pct(100), FOOTER_HEIGHT);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(footer, current_theme.bg_primary, 0);
  lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(footer, 2, 0);
  lv_obj_set_style_border_color(footer, current_theme.border_interface, 0);
  lv_obj_set_style_radius(footer, 0, 0);
  lv_obj_set_style_pad_all(footer, 0, 0);
}