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

#ifndef PSK_QR_UI_H
#define PSK_QR_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Full-parent PSK view; NULL PSK shows an error, QR failure keeps the text key usable. */
lv_obj_t *psk_qr_ui_create(lv_obj_t *parent, const char *psk, const char *status);
void psk_qr_ui_toggle_key(lv_obj_t *view);
void psk_qr_ui_set_status(lv_obj_t *view, const char *status);

#ifdef __cplusplus
}
#endif

#endif // PSK_QR_UI_H
