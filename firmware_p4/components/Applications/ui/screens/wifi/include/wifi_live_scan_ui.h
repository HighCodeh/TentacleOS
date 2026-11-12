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

#ifndef WIFI_LIVE_SCAN_UI_H
#define WIFI_LIVE_SCAN_UI_H

#include "wifi_live_scan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called on the LVGL task after scanning stops; records remain available until the next start. */
typedef void (*wifi_live_scan_ui_done_cb_t)(void *ctx);
esp_err_t wifi_live_scan_ui_open(uint8_t band, wifi_live_scan_ui_done_cb_t done_cb, void *ctx);

bool wifi_live_scan_ui_change_page(uint16_t *page, uint16_t page_size, bool forward);
void wifi_live_scan_ui_set_limit(uint32_t duration_ms, bool single_sweep);

#ifdef __cplusplus
}
#endif
#endif
