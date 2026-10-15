// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A bounded, allocation-free software renderer. The UI owns both PSRAM planes.
#define FLY_LOCAL3D_WIDTH    240U
#define FLY_LOCAL3D_HEIGHT   240U
#define FLY_LOCAL3D_PIXELS   (FLY_LOCAL3D_WIDTH * FLY_LOCAL3D_HEIGHT)
#define FLY_LOCAL3D_FRAME_MS 100U

typedef struct {
  float yaw;
  float pitch;
  uint32_t time_ms;
  uint32_t action_ms;
  bool paused;
} fly_local3d_state_t;

// Optional real firmware/screenshot texture, owned by the UI (never by renderer).
// Native-endian RGB565; stride and capacity are measured in uint16_t pixels.
typedef struct {
  const uint16_t *pixels;
  size_t capacity_pixels;
  uint16_t width;
  uint16_t height;
  uint16_t stride_pixels;
} fly_local3d_texture_t;

#define FLY_LOCAL3D_TEXTURE_MAX_WIDTH  240U
#define FLY_LOCAL3D_TEXTURE_MAX_HEIGHT 320U

void fly_local3d_reset(fly_local3d_state_t *state);
void fly_local3d_camera(fly_local3d_state_t *state, int horizontal, int vertical);
void fly_local3d_interact(fly_local3d_state_t *state);
void fly_local3d_advance(fly_local3d_state_t *state, uint32_t elapsed_ms);
// RGB565 native-endian pixels, a uint16_t inverse-depth plane, and their common
// capacity in pixels. False means invalid input; no output is touched on failure.
// NULL texture (or texture->pixels == NULL) renders a black, empty mini screen.
// A non-null malformed texture is rejected before touching either output plane.
bool fly_local3d_render(const fly_local3d_state_t *state,
                        const fly_local3d_texture_t *screen_texture,
                        uint16_t *pixels,
                        uint16_t *depth,
                        size_t capacity,
                        unsigned width,
                        unsigned height);
