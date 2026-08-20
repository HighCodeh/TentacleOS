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

#ifndef IMAGE_VIEWER_UI_H
#define IMAGE_VIEWER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the image/GIF file to view. Call BEFORE switching to
 *        SCREEN_IMAGE_VIEWER (e.g. from the Files screen). The string is copied.
 *        On open the viewer also scans that file's folder for sibling images so
 *        LEFT/RIGHT flip through them.
 *
 * @param path Absolute path to the image/GIF file to display.
 */
void ui_image_viewer_set_path(const char *path);

/**
 * @brief Set the screen to return to on BACK.
 *
 * @param screen Screen to load on BACK, as a screen_id_t value.
 */
void ui_image_viewer_set_return(int screen);

/**
 * @brief Open the immersive viewer: fills the screen with the image (JPEG via
 *        the LVGL software decoder, PNG via lodepng, animated GIF via lv_gif),
 *        with a slim filename header and a position/hint footer.
 */
void ui_image_viewer_open(void);

/**
 * @brief Free the decode buffers held for the current image. Registered as the
 *        screen close hook so navigating away releases PSRAM cleanly.
 */
void ui_image_viewer_stop(void);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_VIEWER_UI_H
