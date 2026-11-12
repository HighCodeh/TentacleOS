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

#include "imu_monitor_ui.h"
#include "imu_motion.h"
#include "imu_highboy_mesh.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "power_policy.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FRAME_MS        33
#define TEXT_MS         200
#define STALE_US        500000
#define VIEW_MAX_HEIGHT 178
#define VIEW_MARGIN     8
#define STATUS_Y        46
#define VIEW_Y          66
#define LABEL_HEIGHT    20
#define FOOTER_RESERVE  28
#define MODEL_SCALE     73.0f
#define CAMERA_DISTANCE 4.0f
#define MESH_UNITS      1000.0f
#define AXIS_SIZE       19.0f
static lv_obj_t *s_screen, *s_view, *s_status, *s_values, *s_hint;
static lv_timer_t *s_timer;
static bool s_data_view, s_running;
static uint32_t s_text_tick;
static float s_xyz[IMU_MESH_VERTICES][3];
static lv_point_precise_t s_projected[IMU_MESH_VERTICES];
static uint16_t *s_pixels, *s_depth;
static imu_mesh_t *s_mesh;
static const char *TAG = "IMU3D";
static float s_camera[9];
static imu_motion_snapshot_t s_sample;
#define DEPTH_SCALE 50000.0f
_Static_assert(sizeof(imu_mesh_face_t) == 8, "IMU mesh face ABI");
#define VIEW_PIXELS (s_view_width * s_view_height)
static int32_t s_view_width, s_view_height, s_values_y, s_hint_y;
static float s_model_scale;

static bool load_model(void) {
  FILE *file = fopen(IMU_MESH_PATH, "rb");
  if (file == NULL)
    return false;
  uint32_t header[4];
  bool valid = fread(header, sizeof(header), 1, file) == 1 && header[0] == IMU_MESH_MAGIC &&
               header[1] == IMU_MESH_VERTICES && header[2] == IMU_MESH_FACES &&
               header[3] == IMU_MESH_COLORS &&
               fread(s_mesh->vertices, sizeof(s_mesh->vertices), 1, file) == 1 &&
               fread(s_mesh->faces, sizeof(s_mesh->faces), 1, file) == 1 &&
               fread(s_mesh->palette, sizeof(s_mesh->palette), 1, file) == 1 &&
               fgetc(file) == EOF && !ferror(file);
  fclose(file);
  for (unsigned i = 0; valid && i < IMU_MESH_VERTICES; i++)
    for (unsigned j = 0; j < 3; j++)
      if (s_mesh->vertices[i][j] < -IMU_MESH_COORD_LIMIT ||
          s_mesh->vertices[i][j] > IMU_MESH_COORD_LIMIT)
        valid = false;
  for (unsigned i = 0; valid && i < IMU_MESH_FACES; i++) {
    const imu_mesh_face_t *face = &s_mesh->faces[i];
    valid = face->a < IMU_MESH_VERTICES && face->b < IMU_MESH_VERTICES &&
            face->c < IMU_MESH_VERTICES && face->material < IMU_MESH_COLORS;
  }
  return valid;
}

static void raster_triangle(const imu_mesh_face_t *f, uint16_t color) {
  lv_point_precise_t a = s_projected[f->a], b = s_projected[f->b], c = s_projected[f->c];
  float determinant = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
  if (fabsf(determinant) < 0.01f)
    return;
  int left = fmaxf(0, floorf(fminf(a.x, fminf(b.x, c.x))));
  int right = fminf(s_view_width - 1, ceilf(fmaxf(a.x, fmaxf(b.x, c.x))));
  int top = fmaxf(0, floorf(fminf(a.y, fminf(b.y, c.y))));
  int bottom = fminf(s_view_height - 1, ceilf(fmaxf(a.y, fmaxf(b.y, c.y))));
  float da = DEPTH_SCALE / (CAMERA_DISTANCE - s_xyz[f->a][2]);
  float db = DEPTH_SCALE / (CAMERA_DISTANCE - s_xyz[f->b][2]);
  float dc = DEPTH_SCALE / (CAMERA_DISTANCE - s_xyz[f->c][2]);
  float dx_a = (b.y - c.y) / determinant, dx_b = (c.y - a.y) / determinant;
  for (int y = top; y <= bottom; ++y) {
    float wa = ((b.y - c.y) * (left + 0.5f - c.x) + (c.x - b.x) * (y + 0.5f - c.y)) / determinant;
    float wb = ((c.y - a.y) * (left + 0.5f - c.x) + (a.x - c.x) * (y + 0.5f - c.y)) / determinant;
    for (int x = left; x <= right; ++x, wa += dx_a, wb += dx_b) {
      float wc = 1 - wa - wb;
      if (wa >= -0.0001f && wb >= -0.0001f && wc >= -0.0001f) {
        uint16_t depth = (uint16_t)(wa * da + wb * db + wc * dc);
        unsigned pos = y * s_view_width + x;
        if (depth >= s_depth[pos]) {
          s_depth[pos] = depth;
          s_pixels[pos] = color;
        }
      }
    }
  }
}
static void prepare_model(void) {
  /* Fixed camera tilt makes the housing thickness visible at the neutral pose. */
  static const float view[9] = {
      .939693f, -.071110f, -.334546f, 0, .978148f, -.207912f, .342020f, .195373f, .919158f};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col) {
      s_camera[row * 3 + col] = 0;
      for (int k = 0; k < 3; ++k)
        s_camera[row * 3 + col] += view[row * 3 + k] * s_sample.matrix[k * 3 + col];
    }
  for (unsigned i = 0; i < IMU_MESH_VERTICES; ++i) {
    for (int row = 0; row < 3; ++row) {
      s_xyz[i][row] = 0;
      for (int col = 0; col < 3; ++col)
        s_xyz[i][row] += s_camera[row * 3 + col] * s_mesh->vertices[i][col] / MESH_UNITS;
    }
    float perspective = CAMERA_DISTANCE / (CAMERA_DISTANCE - s_xyz[i][2]);
    s_projected[i].x = s_view_width / 2 + s_xyz[i][0] * s_model_scale * perspective;
    s_projected[i].y = s_view_height / 2 - s_xyz[i][1] * s_model_scale * perspective;
  }
  memset(s_depth, 0, VIEW_PIXELS * sizeof(*s_depth));
  for (unsigned i = 0; i < VIEW_PIXELS; ++i)
    s_pixels[i] = 0x0863;
  for (unsigned i = 0; i < IMU_MESH_FACES; ++i) {
    const imu_mesh_face_t *f = &s_mesh->faces[i];
    float *a = s_xyz[f->a], *b = s_xyz[f->b], *c = s_xyz[f->c];
    float ab[3], ac[3], normal[3];
    for (int k = 0; k < 3; ++k) {
      ab[k] = b[k] - a[k];
      ac[k] = c[k] - a[k];
    }
    for (int k = 0; k < 3; ++k)
      normal[k] = ab[(k + 1) % 3] * ac[(k + 2) % 3] - ab[(k + 2) % 3] * ac[(k + 1) % 3];
    if (normal[0] * -a[0] + normal[1] * -a[1] + normal[2] * (CAMERA_DISTANCE - a[2]) <= 0)
      continue;
    float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (length < 0.000001f)
      continue;
    float light = fmaxf(0, (normal[0] * -.3f + normal[1] * .4f + normal[2] * .866f) / length);
    float shade = .38f + .62f * light;
    uint32_t color = s_mesh->palette[f->material];
    if (f->material == 4)
      shade = 1;
    uint8_t red = ((color >> 16) & 255) * shade, green = ((color >> 8) & 255) * shade,
            blue = (color & 255) * shade;
    raster_triangle(f, ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
  }
}
static void draw(lv_event_t *event) {
  if (!s_running || s_data_view || !s_mesh)
    return;
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_area_t area;
  lv_obj_get_coords(s_view, &area);
  static const uint32_t colors[3] = {0xFF6D88, 0x59E2B4, 0x6CAFFF};
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.width = 2;
  line.opa = LV_OPA_COVER;
  for (int i = 0; i < 3; ++i) {
    line.color = lv_color_hex(colors[i]);
    line.p1.x = area.x1 + s_view_width - 28;
    line.p1.y = area.y2 - 24;
    line.p2.x = line.p1.x + AXIS_SIZE * s_camera[i];
    line.p2.y = line.p1.y - AXIS_SIZE * s_camera[3 + i];
    lv_draw_line(layer, &line);
  }
}
static void refresh(lv_timer_t *timer) {
  (void)timer;
  if (!s_running || lv_screen_active() != s_screen)
    return;
  imu_motion_snapshot(&s_sample);
  if (!s_data_view && s_pixels && s_depth) {
    prepare_model();
    lv_obj_invalidate(s_view);
  }
  uint32_t now = lv_tick_get();
  if (now - s_text_tick < TEXT_MS)
    return;
  s_text_tick = now;
  bool stale = s_sample.sample_us && esp_timer_get_time() - s_sample.sample_us > STALE_US;
  char text[240];
  if (s_sample.error)
    snprintf(text, sizeof(text), "SENSOR ERROR 0x%X", (unsigned)s_sample.error);
  else if (stale)
    snprintf(text, sizeof(text), "NO FRESH DATA / PAUSED");
  else if (!s_sample.sample_us)
    snprintf(text, sizeof(text), "SENSOR WARMING UP...");
  else if (s_sample.zeroing)
    snprintf(text, sizeof(text), "HOLD STILL  %lu/160", (unsigned long)s_sample.zero_count);
  else
    snprintf(text, sizeof(text), "LIVE  %.0f Hz  |  RELATIVE", s_sample.rate_hz);
  lv_label_set_text(s_status, text);
  lv_obj_set_style_text_color(
      s_status, (stale || s_sample.error) ? lv_color_hex(0xFF6D88) : lv_color_hex(0x59E2B4), 0);
  if (s_data_view) {
    snprintf(text,
             sizeof(text),
             "ACCEL g: X %+.3f Y %+.3f\nZ %+.3f\nGYRO deg/s: X %+.2f\nY %+.2f Z %+.2f\nDIE %.1f C  "
             "ERR %lu",
             s_sample.accel[0],
             s_sample.accel[1],
             s_sample.accel[2],
             s_sample.gyro[0],
             s_sample.gyro[1],
             s_sample.gyro[2],
             s_sample.temperature,
             (unsigned long)s_sample.errors);
    lv_label_set_text(s_values, text);
  } else {
    snprintf(text,
             sizeof(text),
             "X %+.0f    Y %+.0f    Z %+.0f",
             s_sample.angles[0],
             s_sample.angles[1],
             s_sample.angles[2]);
    lv_label_set_text(s_values, text);
  }
}
void ui_imu_monitor_stop(void) {
  s_running = false;
  if (s_timer) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_screen)
    power_policy_set_display_hold(s_screen, false);
  esp_err_t err = imu_motion_stop();
  if (err != ESP_OK)
    ESP_LOGW("IMU3D", "Stop: %s", esp_err_to_name(err));
}
static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  ui_imu_monitor_stop();
  free(s_pixels);
  free(s_depth);
  free(s_mesh);
  s_mesh = NULL;
  s_pixels = s_depth = NULL;
  s_screen = s_view = s_status = s_values = s_hint = NULL;
}
static void input(const input_event_t *event, void *context) {
  (void)context;
  if (!s_running || event->action != INPUT_ACTION_PRESS)
    return;
  if (event->button == INPUT_BTN_BACK)
    ui_switch_screen(SCREEN_SETTINGS);
  else if (event->button == INPUT_BTN_OK)
    imu_motion_request(false);
  else if (event->button == INPUT_BTN_UP)
    imu_motion_request(true);
  else if (event->button == INPUT_BTN_RIGHT) {
    s_data_view = !s_data_view;
    lv_obj_set_pos(s_values, VIEW_MARGIN, s_data_view ? VIEW_Y : s_values_y);
    lv_obj_set_width(s_values, ui_screen_w() - 2 * VIEW_MARGIN);
    lv_label_set_text(s_values, "");
    if (s_data_view)
      lv_obj_add_flag(s_view, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(s_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_screen);
    s_text_tick = lv_tick_get() - TEXT_MS;
  }
}
static lv_obj_t *label(int y, const char *text, lv_color_t color) {
  lv_obj_t *obj = lv_label_create(s_screen);
  lv_obj_set_width(obj, ui_screen_w() - 2 * VIEW_MARGIN);
  lv_obj_set_pos(obj, VIEW_MARGIN, y);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(obj, color, 0);
  lv_label_set_text(obj, text);
  return obj;
}
void ui_imu_monitor_log(void) {
  imu_motion_log();
}
void ui_imu_monitor_open(void) {
  ui_imu_monitor_stop();
  if (s_screen)
    lv_obj_delete(s_screen);
  s_data_view = false;
  s_text_tick = 0;
  s_screen = lv_obj_create(NULL);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x080D18), 0);
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  ui_chrome_header(s_screen, "HIGH BOY / 3D", "/assets/icons/sensors.bin");
  s_hint_y = ui_screen_h() - FOOTER_RESERVE - LABEL_HEIGHT;
  s_values_y = s_hint_y - LABEL_HEIGHT;
  s_view_width = ui_screen_w() - 2 * VIEW_MARGIN;
  s_view_height = s_values_y - VIEW_Y;
  if (s_view_height > VIEW_MAX_HEIGHT)
    s_view_height = VIEW_MAX_HEIGHT;
  s_model_scale = MODEL_SCALE * s_view_height / VIEW_MAX_HEIGHT;
  s_status = label(STATUS_Y, "SENSOR WARMING UP...", lv_color_hex(0x59E2B4));
  s_view = lv_canvas_create(s_screen);
  lv_obj_remove_style_all(s_view);
  lv_obj_set_size(s_view, s_view_width, s_view_height);
  lv_obj_set_pos(s_view, VIEW_MARGIN, VIEW_Y);
  lv_obj_remove_flag(s_view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_view, draw, LV_EVENT_DRAW_MAIN, NULL);
  s_values = label(s_values_y, "X --    Y --    Z --", lv_color_hex(0xE1E7F2));
  s_hint = label(s_hint_y, "UP Calibrate | RIGHT Data/3D", lv_color_hex(0x99A9C4));
  ui_chrome_footer(s_screen, "OK Center     BACK Exit");
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);

  s_running = true;
  s_pixels = heap_caps_malloc(VIEW_PIXELS * sizeof(*s_pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_depth = heap_caps_malloc(VIEW_PIXELS * sizeof(*s_depth), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_mesh = heap_caps_malloc(sizeof(*s_mesh), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_pixels || !s_depth || !s_mesh) {
    ESP_LOGE(TAG, "IMU view allocation failed");
    lv_label_set_text(s_status, "NOT ENOUGH MEMORY");
    goto cleanup;
  }
  if (!load_model()) {
    ESP_LOGE(TAG, "IMU model missing or invalid");
    lv_label_set_text(s_status, "MODEL FILE MISSING / INVALID");
    goto cleanup;
  }
  s_timer = lv_timer_create(refresh, FRAME_MS, NULL);
  if (s_timer == NULL) {
    ESP_LOGE(TAG, "IMU refresh timer allocation failed");
    lv_label_set_text(s_status, "REFRESH TIMER UNAVAILABLE");
    goto cleanup;
  }
  esp_err_t err = imu_motion_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "IMU worker start failed: %s", esp_err_to_name(err));
    lv_label_set_text(s_status, "SENSOR COULD NOT START");
    goto cleanup;
  }
  for (unsigned i = 0; i < VIEW_PIXELS; ++i)
    s_pixels[i] = 0x0863;
  lv_canvas_set_buffer(s_view, s_pixels, s_view_width, s_view_height, LV_COLOR_FORMAT_RGB565);
  power_policy_set_display_hold(s_screen, true);
  return;
cleanup:
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  free(s_pixels);
  free(s_depth);
  free(s_mesh);
  s_pixels = s_depth = NULL;
  s_mesh = NULL;
}
