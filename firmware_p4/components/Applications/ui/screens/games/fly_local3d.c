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

#include "fly_local3d.h"
#include "fly_highboy_mesh.h"

#include <math.h>
#include <string.h>

#define PI        3.14159265358979323846f
#define RING      10
#define LATITUDE  6
#define TUBE_RING 5

typedef struct {
  float x, y, z;
} vec3;
typedef struct {
  float x, y, inverse_z;
} projected;
typedef struct {
  uint16_t *pixels, *depth;
  unsigned width, height;
  float sy, cy, sp, cp, focal;
} renderer;

static vec3 add(vec3 a, vec3 b) {
  return (vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}
static vec3 sub(vec3 a, vec3 b) {
  return (vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}
static vec3 scale(vec3 a, float k) {
  return (vec3){a.x * k, a.y * k, a.z * k};
}
static float dot(vec3 a, vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
static vec3 cross(vec3 a, vec3 b) {
  return (vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static vec3 unit(vec3 a) {
  float length = sqrtf(dot(a, a));
  return length > 0.00001f ? scale(a, 1.0f / length) : (vec3){0, 1, 0};
}
static float clamp(float x, float lo, float hi) {
  return fminf(hi, fmaxf(lo, x));
}

void fly_local3d_reset(fly_local3d_state_t *s) {
  if (s == NULL)
    return;
  *s = (fly_local3d_state_t){.yaw = 2.10f, .pitch = 0.55f};
}
void fly_local3d_camera(fly_local3d_state_t *s, int horizontal, int vertical) {
  if (s == NULL)
    return;
  // Only the sign matters: external integer magnitude cannot overflow.
  s->yaw += horizontal > 0 ? 0.14f : horizontal < 0 ? -0.14f : 0;
  if (s->yaw > PI)
    s->yaw -= 2 * PI;
  if (s->yaw < -PI)
    s->yaw += 2 * PI;
  s->pitch = clamp(s->pitch + (vertical > 0 ? 0.10f : vertical < 0 ? -0.10f : 0), -0.15f, 1.1f);
}
void fly_local3d_interact(fly_local3d_state_t *s) {
  if (s == NULL)
    return;
  s->action_ms = 3800;
  s->paused = false;
}
void fly_local3d_advance(fly_local3d_state_t *s, uint32_t elapsed_ms) {
  if (s == NULL || s->paused)
    return;
  // No catch-up burst after task suspension or tick rollover.
  uint32_t dt = elapsed_ms > 200 ? 200 : elapsed_ms;
  s->time_ms = (s->time_ms % 120000U + dt) % 120000U;
  s->action_ms = s->action_ms > dt ? s->action_ms - dt : 0;
}

static projected project(const renderer *r, vec3 v) {
  float x = r->cy * v.x - r->sy * v.z;
  float near = r->sy * v.x + r->cy * v.z;
  float y = r->cp * (v.y - 0.22f) - r->sp * near;
  float z = 4.3f - r->sp * (v.y - 0.22f) - r->cp * near;
  return (projected){
      r->width * 0.5f + r->focal * x / z, r->height * 0.5f - r->focal * y / z, 1.0f / z};
}
static float edge(projected a, projected b, float x, float y) {
  return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
static uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static uint16_t shade(uint32_t rgb, float illumination) {
  unsigned r = (unsigned)(((rgb >> 16) & 255U) * illumination);
  unsigned g = (unsigned)(((rgb >> 8) & 255U) * illumination);
  unsigned b = (unsigned)((rgb & 255U) * illumination);
  return rgb565(r, g, b);
}
static uint16_t blend(uint16_t a, uint16_t b) {
  // Equal blend per component without carry into adjacent RGB565 channels.
  return (uint16_t)(((a & 0xf7deU) + (b & 0xf7deU)) >> 1);
}
typedef struct {
  float u, v;
} uv;
static void raster_triangle(renderer *r,
                            projected p,
                            projected q,
                            projected t,
                            uint16_t color,
                            bool translucent,
                            const fly_local3d_texture_t *texture,
                            uv puv,
                            uv quv,
                            uv tuv) {
  float area = edge(p, q, t.x, t.y);
  if (fabsf(area) < 0.001f)
    return;
  int x0 = (int)floorf(fminf(p.x, fminf(q.x, t.x)));
  int x1 = (int)ceilf(fmaxf(p.x, fmaxf(q.x, t.x)));
  int y0 = (int)floorf(fminf(p.y, fminf(q.y, t.y)));
  int y1 = (int)ceilf(fmaxf(p.y, fmaxf(q.y, t.y)));
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= (int)r->width)
    x1 = (int)r->width - 1;
  if (y1 >= (int)r->height)
    y1 = (int)r->height - 1;
  const float reciprocal = 1.0f / area;
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      float u = edge(q, t, x + 0.5f, y + 0.5f) * reciprocal;
      float v = edge(t, p, x + 0.5f, y + 0.5f) * reciprocal;
      float w = 1.0f - u - v;
      if (u < 0 || v < 0 || w < 0)
        continue;
      float inverse_z = u * p.inverse_z + v * q.inverse_z + w * t.inverse_z;
      uint16_t z = (uint16_t)(inverse_z * 60000.0f);
      size_t at = (size_t)y * r->width + (unsigned)x;
      if (z <= r->depth[at])
        continue;
      r->depth[at] = z;
      uint16_t fragment = color;
      if (texture != NULL && texture->pixels != NULL) {
        // Perspective-correct interpolation: UV/z divided by interpolated 1/z.
        float tx = (u * puv.u * p.inverse_z + v * quv.u * q.inverse_z + w * tuv.u * t.inverse_z) /
                   inverse_z;
        float ty = (u * puv.v * p.inverse_z + v * quv.v * q.inverse_z + w * tuv.v * t.inverse_z) /
                   inverse_z;
        unsigned ix = (unsigned)clamp(tx * texture->width, 0, texture->width - 1U);
        unsigned iy = (unsigned)clamp(ty * texture->height, 0, texture->height - 1U);
        fragment = texture->pixels[(size_t)iy * texture->stride_pixels + ix];
      }
      r->pixels[at] = translucent ? blend(r->pixels[at], fragment) : fragment;
    }
  }
}
static void triangle(renderer *r, vec3 a, vec3 b, vec3 c, uint32_t rgb, bool translucent) {
  vec3 normal = unit(cross(sub(b, a), sub(c, a)));
  const vec3 light = {-0.38f, 0.76f, 0.52f};
  float intensity = 0.30f + 0.70f * fmaxf(0, dot(normal, light));
  if (translucent)
    intensity = 0.70f + 0.30f * fabsf(dot(normal, light));
  raster_triangle(r,
                  project(r, a),
                  project(r, b),
                  project(r, c),
                  shade(rgb, intensity),
                  translucent,
                  NULL,
                  (uv){0, 0},
                  (uv){0, 0},
                  (uv){0, 0});
}

// The CAD is mm, +Y top, +Z front. Rotate its screen toward the fly (-Z),
// tilted upward, so the device isn't shown backwards in its forelegs.
static vec3 device_transform(vec3 mm) {
  const float size = 0.0105f, cosine = 0.51481885f, sine = 0.85729899f;
  return (vec3){-mm.x * size,
                0.03f + (cosine * mm.y + sine * mm.z) * size,
                1.05f + (sine * mm.y - cosine * mm.z) * size};
}
static vec3 device_anchor(const float mm[3]) {
  return device_transform((vec3){mm[0], mm[1], mm[2]});
}
static void highboy(renderer *r, const fly_local3d_texture_t *texture) {
  static const uint32_t colors[] = {0xA065DE, 0x7443AA, 0x20172D, 0x342342, 0x231C2F};
  for (unsigned part = 0; part < FLY_HIGHBOY_PART_COUNT; part++) {
    const fly_highboy_part_t *range = &fly_highboy_parts[part];
    for (unsigned i = range->first_triangle; i < range->first_triangle + range->triangle_count;
         i++) {
      vec3 world[3];
      for (unsigned j = 0; j < 3; j++) {
        const int16_t *p = fly_highboy_vertices[fly_highboy_triangles[i][j]];
        world[j] = device_transform((vec3){p[0] * FLY_HIGHBOY_VERTEX_SCALE,
                                           p[1] * FLY_HIGHBOY_VERTEX_SCALE,
                                           p[2] * FLY_HIGHBOY_VERTEX_SCALE});
      }
      triangle(r, world[0], world[1], world[2], colors[range->material], false);
    }
  }
  const float *center = fly_highboy_screen_center;
  float half_w = FLY_HIGHBOY_SCREEN_WIDTH_MM * 0.5f, half_h = FLY_HIGHBOY_SCREEN_HEIGHT_MM * 0.5f;
  vec3 tl = device_transform((vec3){center[0] - half_w, center[1] + half_h, center[2]});
  vec3 tr = device_transform((vec3){center[0] + half_w, center[1] + half_h, center[2]});
  vec3 br = device_transform((vec3){center[0] + half_w, center[1] - half_h, center[2]});
  vec3 bl = device_transform((vec3){center[0] - half_w, center[1] - half_h, center[2]});
  raster_triangle(r,
                  project(r, tl),
                  project(r, tr),
                  project(r, br),
                  0,
                  false,
                  texture,
                  (uv){0, 0},
                  (uv){1, 0},
                  (uv){1, 1});
  raster_triangle(r,
                  project(r, tl),
                  project(r, br),
                  project(r, bl),
                  0,
                  false,
                  texture,
                  (uv){0, 0},
                  (uv){1, 1},
                  (uv){0, 1});
}

static void ellipsoid(renderer *r, vec3 center, vec3 radii, uint32_t rgb) {
  vec3 rings[LATITUDE + 1][RING];
  for (int y = 0; y <= LATITUDE; y++) {
    float latitude = PI * y / LATITUDE;
    for (int x = 0; x < RING; x++) {
      float longitude = 2 * PI * x / RING;
      rings[y][x] = add(center,
                        (vec3){radii.x * sinf(latitude) * cosf(longitude),
                               radii.y * cosf(latitude),
                               radii.z * sinf(latitude) * sinf(longitude)});
    }
  }
  for (int y = 0; y < LATITUDE; y++)
    for (int x = 0; x < RING; x++) {
      int next = (x + 1) % RING;
      triangle(r, rings[y][x], rings[y][next], rings[y + 1][next], rgb, false);
      triangle(r, rings[y][x], rings[y + 1][next], rings[y + 1][x], rgb, false);
    }
}
static void tube(renderer *r, vec3 a, vec3 b, float radius, uint32_t rgb) {
  vec3 axis = unit(sub(b, a));
  vec3 normal = unit(cross(axis, fabsf(axis.y) > 0.9f ? (vec3){1, 0, 0} : (vec3){0, 1, 0}));
  vec3 tangent = cross(axis, normal), aa[TUBE_RING], bb[TUBE_RING];
  for (int i = 0; i < TUBE_RING; i++) {
    float theta = 2 * PI * i / TUBE_RING;
    vec3 rim = scale(add(scale(normal, cosf(theta)), scale(tangent, sinf(theta))), radius);
    aa[i] = add(a, rim);
    bb[i] = add(b, scale(rim, 0.72f));
  }
  for (int i = 0; i < TUBE_RING; i++) {
    int n = (i + 1) % TUBE_RING;
    triangle(r, aa[i], aa[n], bb[n], rgb, false);
    triangle(r, aa[i], bb[n], bb[i], rgb, false);
  }
}
static void wing(renderer *r, float side, float flap) {
  // Real planar polygon with a local hinge. Draw after the opaque body.
  const vec3 anchor = {side * 0.12f, 0.39f, 0.04f};
  const vec3 shape[] = {{0, 0, 0},
                        {0.43f, 0.10f, -0.13f},
                        {0.99f, 0.06f, -0.59f},
                        {1.13f, 0, -0.95f},
                        {0.82f, -0.015f, -1.03f},
                        {0.24f, 0, -0.53f}};
  vec3 points[6];
  for (int i = 0; i < 6; i++) {
    float x = shape[i].x * cosf(flap), y = shape[i].x * sinf(flap) + shape[i].y;
    points[i] = add(anchor, (vec3){side * x, y, shape[i].z});
  }
  for (int i = 1; i < 5; i++)
    triangle(r, points[0], points[i], points[i + 1], 0xC3BCEB, true);
  for (int i = 0; i < 6; i++)
    tube(r, points[i], points[(i + 1) % 6], 0.009f, 0x88819F);
  tube(r, points[0], points[3], 0.008f, 0x9386B0);
  tube(r, points[1], points[4], 0.007f, 0x75688E);
}

bool fly_local3d_render(const fly_local3d_state_t *s,
                        const fly_local3d_texture_t *texture,
                        uint16_t *pixels,
                        uint16_t *depth,
                        size_t capacity,
                        unsigned width,
                        unsigned height) {
  if (s == NULL || pixels == NULL || depth == NULL || pixels == depth || width == 0 ||
      height == 0 || width > FLY_LOCAL3D_WIDTH || height > FLY_LOCAL3D_HEIGHT ||
      capacity < (size_t)width * height || !isfinite(s->yaw) || !isfinite(s->pitch) ||
      fabsf(s->yaw) > 4 * PI || s->pitch < -0.2f || s->pitch > 1.2f)
    return false;
  if (texture != NULL && texture->pixels != NULL &&
      (texture->width == 0 || texture->height == 0 ||
       texture->width > FLY_LOCAL3D_TEXTURE_MAX_WIDTH ||
       texture->height > FLY_LOCAL3D_TEXTURE_MAX_HEIGHT ||
       texture->stride_pixels < texture->width ||
       texture->stride_pixels > FLY_LOCAL3D_TEXTURE_MAX_WIDTH ||
       texture->capacity_pixels <
           (size_t)(texture->height - 1U) * texture->stride_pixels + texture->width))
    return false;
  size_t bytes = (size_t)width * height * sizeof(uint16_t);
  uintptr_t p = (uintptr_t)pixels, d = (uintptr_t)depth;
  if ((p < d && d - p < bytes) || (d < p && p - d < bytes))
    return false;
  if (texture != NULL && texture->pixels != NULL) {
    uintptr_t source = (uintptr_t)texture->pixels;
    size_t texture_bytes =
        ((size_t)(texture->height - 1U) * texture->stride_pixels + texture->width) * 2U;
    if ((source <= p ? p - source < texture_bytes : source - p < bytes) ||
        (source <= d ? d - source < texture_bytes : source - d < bytes))
      return false;
  }
  memset(pixels, 0, bytes);
  memset(depth, 0, bytes);
  renderer r = {pixels,
                depth,
                width,
                height,
                sinf(s->yaw),
                cosf(s->yaw),
                sinf(s->pitch),
                cosf(s->pitch),
                fminf(width, height) * 1.24f};
  float time = (s->time_ms % 120000U) * 0.001f;
  highboy(&r, texture);
  float gait = s->action_ms ? sinf(time * 4.0f) * 0.02f : sinf(time * 1.8f) * 0.008f;
  for (int side = -1; side <= 1; side += 2)
    for (int pair = 0; pair < 3; pair++) {
      float z = 0.32f - pair * 0.37f;
      float stride = gait * ((pair & 1) ? -side : side);
      vec3 shoulder = {side * 0.18f, 0.15f, z};
      if (pair == 0) {
        vec3 foot;
        if (side < 0) {
          // Left foreleg supports the opposite CAD edge after the 180deg yaw.
          const float *grip = fly_highboy_grip_anchor;
          foot = device_transform((vec3){-grip[0], grip[1], grip[2]});
        } else {
          foot = device_anchor(fly_highboy_ok_anchor);
          float lift = 0.020f + (0.5f + 0.5f * sinf(time * (s->action_ms ? 5.0f : 2.1f))) * 0.075f;
          foot = add(foot, (vec3){0, lift * 0.8573f, -lift * 0.5148f});
        }
        vec3 elbow = {side * 0.47f, 0.19f, 0.63f};
        tube(&r, shoulder, elbow, 0.035f, 0x9B7CB8);
        tube(&r, elbow, foot, 0.025f, 0xB7A0D3);
        tube(&r, foot, add(foot, (vec3){-side * 0.045f, 0, -0.020f}), 0.016f, 0x8F76A4);
        continue;
      }
      vec3 knee = {side * (0.53f + (pair == 1 ? 0.08f : 0)), 0.02f, z + 0.20f - pair * 0.13f};
      vec3 ankle = {
          side * (0.79f + (pair == 1 ? 0.12f : 0)), -0.38f, z + 0.45f - pair * 0.37f + stride};
      tube(&r, shoulder, knee, 0.035f, 0x8D849E);
      tube(&r, knee, ankle, 0.026f, 0xBCB0D1);
      tube(&r, ankle, add(ankle, (vec3){side * 0.06f, -0.01f, 0.12f}), 0.016f, 0x82778F);
    }
  ellipsoid(&r, (vec3){0, 0.19f, -0.53f}, (vec3){0.29f, 0.26f, 0.53f}, 0x65428A);
  ellipsoid(&r, (vec3){0, 0.27f, -0.02f}, (vec3){0.28f, 0.30f, 0.34f}, 0x9A70C9);
  ellipsoid(&r, (vec3){0, 0.28f, 0.42f}, (vec3){0.31f, 0.25f, 0.25f}, 0x80608F);
  for (int side = -1; side <= 1; side += 2) {
    ellipsoid(&r, (vec3){side * 0.238f, 0.34f, 0.48f}, (vec3){0.145f, 0.206f, 0.172f}, 0xAF528A);
    ellipsoid(&r, (vec3){side * 0.25f, 0.44f, 0.588f}, (vec3){0.040f, 0.050f, 0.026f}, 0xEEDCEE);
    tube(&r,
         (vec3){side * 0.09f, 0.40f, 0.60f},
         (vec3){side * 0.17f, 0.59f, 0.75f},
         0.015f,
         0xCEBADA);
    tube(&r,
         (vec3){side * 0.17f, 0.59f, 0.75f},
         (vec3){side * 0.25f, 0.63f, 0.80f},
         0.010f,
         0xAA97BD);
  }
  tube(&r, (vec3){0, 0.21f, 0.61f}, (vec3){0, 0.08f, 0.73f}, 0.024f, 0x736272);
  float flap =
      s->action_ms ? 0.22f + sinf(time * 15.0f) * 0.35f : 0.10f + sinf(time * 2.6f) * 0.08f;
  // Back-to-front wing order improves half-opacity at an overlapping hinge.
  float far_side = sinf(s->yaw) > 0 ? -1.0f : 1.0f;
  wing(&r, far_side, flap);
  wing(&r, -far_side, flap);
  return true;
}
