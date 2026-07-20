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

#ifndef NOTIFY_UI_H
#define NOTIFY_UI_H

#ifdef __cplusplus
extern "C" {
#endif

// Global toast notification: a small pill that appears at the top of the
// screen on the LVGL TOP LAYER, so it floats above ANY screen (it can never be
// overlapped) and survives screen switches. It auto-dismisses after a few
// seconds and does not steal input from the active screen. A new call replaces
// the one currently showing. Keep the text short (it's a one-liner).
typedef enum {
  NOTIFY_INFO = 0, // purple — generic (paired, connected, ...)
  NOTIFY_SAVED,    // green check — saved / applied confirmation
  NOTIFY_UPDATE,   // green  — firmware / update available
  NOTIFY_LORA,     // cyan   — LoRa / incoming message
  NOTIFY_WARNING,  // amber  — battery low, C5 dropped, ...
} notify_type_t;

/** Show a short notification pill. Safe to call from any UI callback. */
void notify(notify_type_t type, const char *text);

#ifdef __cplusplus
}
#endif

#endif // NOTIFY_UI_H
