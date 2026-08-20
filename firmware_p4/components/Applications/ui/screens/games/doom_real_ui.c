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

#include "doom_real_ui.h"

#include "ui_manager.h"   // lvgl.h + ui_screen_load()
#include "ui_liveness.h"  // ui_render_beat_kick()
#include "doom_highboy.h" // highboy_doom_start() (doom component)

void ui_doom_real_open(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, "DOOM\nloading /sdcard/doom/doom1.wad ...");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xC8C8C8), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(lbl);

  ui_screen_load(scr);

  // The DOOM task waits ~150ms for this screen to settle, then enters direct
  // draw mode and owns the panel. Quitting DOOM reboots, so no teardown here.
  highboy_doom_start(ui_render_beat_kick);
}
