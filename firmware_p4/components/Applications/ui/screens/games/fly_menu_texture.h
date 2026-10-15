// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLY_MENU_TEXTURE_WIDTH       96U
#define FLY_MENU_TEXTURE_HEIGHT      128U
#define FLY_MENU_TEXTURE_PIXELS      (FLY_MENU_TEXTURE_WIDTH * FLY_MENU_TEXTURE_HEIGHT)
#define FLY_MENU_TEXTURE_INTERVAL_MS 1800U

// UI/LVGL task only. This renders the real menu/catalog as a read-only demo
// loop, NOT independent firmware execution and NOT remote control of radios.
// App-owned buffers/widgets are allocated once at open; LVGL's own renderer
// may allocate internal draw tasks/decoder scratch during snapshots.
bool fly_menu_texture_open(void);
bool fly_menu_texture_tick(uint32_t elapsed_ms);
const uint16_t *fly_menu_texture_pixels(void); // NULL => use a black screen.
void fly_menu_texture_close(void);

// Bounded pure helper, also used by host tests. Native-endian RGB565.
bool fly_menu_texture_downsample(const uint16_t *source,
                                 size_t source_capacity,
                                 unsigned source_width,
                                 unsigned source_height,
                                 unsigned source_stride,
                                 uint16_t *destination,
                                 size_t destination_capacity);
