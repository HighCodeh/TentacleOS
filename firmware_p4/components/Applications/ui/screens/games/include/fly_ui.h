// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FLY_UI_H
#define FLY_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/** Open the native Mosca TEST view; no Mac connection is required to navigate.
 */
void ui_fly_open(void);

/** Stop presentation and close its session without waiting for a host reply. */
void ui_fly_stop(void);

#ifdef __cplusplus
}
#endif

#endif // FLY_UI_H
