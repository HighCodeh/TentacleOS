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

#ifndef ERROR_UI_H
#define ERROR_UI_H

#ifdef __cplusplus
extern "C" {
#endif

// Global error banner: a red toast on the LVGL TOP LAYER, so it floats above
// ANY screen (it can never be overlapped) and survives screen switches. Shows
// a short title + a one-line detail, buzzes, and auto-dismisses after a few
// seconds. Non-blocking (never steals input). A new call replaces the current
// one. For soft warnings prefer notify(NOTIFY_WARNING, ...).
void error_show(const char *title, const char *msg);

#ifdef __cplusplus
}
#endif

#endif // ERROR_UI_H
