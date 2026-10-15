// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fly_ui.h"
#include "fly_local3d.h"
#include "fly_menu_texture.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"

#include "fly_link.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "power_policy.h"

#define FLY_TICK_MS                 33U
#define FLY_MARGIN                  8
#define FLY_ARENA_TOP               62
#define FLY_BOTTOM_SPACE            80
#define FLY_ARENA_INSET             18
#define FLY_CURSOR_STEP             2048
#define FLY_CURSOR_HALF             6
#define FLY_CURSOR_START            (FLY_LINK_COORD_MAX / 2U)
#define FLY_LEG_COUNT               6U
#define FLY_WING_COUNT              2U
#define FLY_LINE_POINTS             3U
#define FLY_MENU_COUNT              4U
#define FLY_MENU_HEIGHT             186
#define FLY_MENU_ROW_HEIGHT         27
#define FLY_MENU_ROW_TOP            30
#define FLY_TEXT_SIZE               64U
#define FLY_NOTICE_MS               1500U
#define FLY_PHASE_MS                90U
#define FLY_CENTIDEGREES_PER_DEGREE 100U
#define FLY_MILLISECONDS_PER_SECOND 1000U
#define FLY_SECONDS_PER_MINUTE      60U
#define FLY_ACTIVITY_PERCENT_SCALE  100U
#define FLY_COLOR_BG                0x000000
#define FLY_COLOR_ACCENT            0xBD82FF
#define FLY_COLOR_LIGHT             0xE6D4FF
#define FLY_COLOR_MUTED             0x9580AF
#define FLY_COLOR_GRID              0x2D1B40
#define FLY_COLOR_SELECTED          0x30154C
#define FLY_COLOR_WARNING           0xF2A1F4

typedef enum { FLY_MENU_STIMULUS, FLY_MENU_PAUSE, FLY_MENU_RESET, FLY_MENU_EXIT } fly_menu_item_t;

static const char *TAG = "FLY_UI";
static lv_obj_t *s_screen;
static lv_obj_t *s_legacy;
static lv_obj_t *s_visual;
static lv_obj_t *s_visual_image;
static lv_obj_t *s_visual_state;
static lv_obj_t *s_visual_footer;
static lv_obj_t *s_local;
static lv_obj_t *s_local_image;
static lv_obj_t *s_local_error;
static uint16_t *s_local_planes;
static lv_image_dsc_t s_local_descriptor;
static fly_local3d_state_t s_local_state;
static uint32_t s_local_tick;
static bool s_is_local;
static bool s_local_dirty;
static bool s_local_memory_error;
static uint8_t *s_visual_pixels;
static lv_image_dsc_t s_visual_descriptor;
static uint32_t s_presented_frame;
static bool s_visual_memory_error;
static lv_obj_t *s_arena;
static lv_obj_t *s_status;
static lv_obj_t *s_time;
static lv_obj_t *s_speed;
static lv_obj_t *s_action;
static lv_obj_t *s_waiting;
static lv_obj_t *s_fly;
static lv_obj_t *s_body;
static lv_obj_t *s_head;
static lv_obj_t *s_legs[FLY_LEG_COUNT];
static lv_obj_t *s_wings[FLY_WING_COUNT];
static lv_obj_t *s_stimuli[FLY_LINK_STIMULUS_MAX];
static lv_obj_t *s_stimulus_labels[FLY_LINK_STIMULUS_MAX];
static lv_obj_t *s_bars[FLY_LINK_ACTIVITY_MAX];
static lv_obj_t *s_values[FLY_LINK_ACTIVITY_MAX];
static lv_obj_t *s_cursor[2];
static lv_obj_t *s_menu;
static lv_obj_t *s_menu_rows[FLY_MENU_COUNT];
static lv_obj_t *s_menu_labels[FLY_MENU_COUNT];
static lv_obj_t *s_menu_help;
static lv_timer_t *s_timer;
static lv_point_precise_t s_body_points[2];
static lv_point_precise_t s_leg_points[FLY_LEG_COUNT][FLY_LINE_POINTS];
static lv_point_precise_t s_wing_points[FLY_WING_COUNT][FLY_LINE_POINTS];
static fly_link_view_t s_view;
static uint32_t s_ui_generation;
static uint32_t s_link_generation;
static uint32_t s_presented_sequence;
static uint32_t s_notice_until;
static uint16_t s_cursor_x;
static uint16_t s_cursor_y;
static int32_t s_arena_width;
static int32_t s_arena_height;
static uint8_t s_menu_index;
static fly_link_action_t s_stimulus_action;
static bool s_is_open;
static bool s_is_ready;
static bool s_menu_open;
static bool s_link_error;
static char s_time_text[FLY_TEXT_SIZE];
static char s_speed_text[FLY_TEXT_SIZE];
static char s_action_text[FLY_TEXT_SIZE];
static char s_value_text[FLY_LINK_ACTIVITY_MAX][8];
static const char *s_notice;

static void set_hidden(lv_obj_t *obj, bool hidden) {
  if (hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *
make_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
  lv_obj_t *box = lv_obj_create(parent);
  if (box == NULL)
    return NULL;
  lv_obj_remove_style_all(box);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, w, h);
  lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return box;
}

static lv_obj_t *make_label(
    lv_obj_t *parent, int32_t x, int32_t y, int32_t width, const char *text, uint32_t color) {
  lv_obj_t *label = lv_label_create(parent);
  if (label == NULL)
    return NULL;
  lv_obj_remove_style_all(label);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_label_set_text_static(label, text);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

static lv_obj_t *make_line(lv_obj_t *parent, uint32_t color, int32_t width) {
  lv_obj_t *line = lv_line_create(parent);
  if (line == NULL)
    return NULL;
  lv_obj_remove_style_all(line);
  lv_obj_set_pos(line, 0, 0);
  lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
  lv_obj_set_style_line_width(line, width, 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  return line;
}

static int32_t coordinate_to_pixel(uint16_t coordinate, int32_t length) {
  int32_t span = length - 2 * FLY_ARENA_INSET;
  return FLY_ARENA_INSET + (int32_t)((uint32_t)coordinate * (uint32_t)span / FLY_LINK_COORD_MAX);
}

static void render_cursor(void) {
  int32_t x = coordinate_to_pixel(s_cursor_x, s_arena_width);
  int32_t y = coordinate_to_pixel(s_cursor_y, s_arena_height);
  lv_obj_set_pos(s_cursor[0], x - FLY_CURSOR_HALF, y);
  lv_obj_set_pos(s_cursor[1], x, y - FLY_CURSOR_HALF);
}

static void transform_point(lv_point_precise_t *point,
                            int32_t x,
                            int32_t y,
                            int32_t cx,
                            int32_t cy,
                            int32_t sine,
                            int32_t cosine) {
  point->x = cx + (x * cosine - y * sine) / LV_TRIGO_SIN_MAX;
  point->y = cy + (x * sine + y * cosine) / LV_TRIGO_SIN_MAX;
}

static void render_fly(const fly_link_snapshot_t *snapshot) {
  int32_t x = coordinate_to_pixel(snapshot->x, s_arena_width);
  int32_t y = coordinate_to_pixel(snapshot->y, s_arena_height);
  int16_t degrees = (int16_t)(snapshot->heading / FLY_CENTIDEGREES_PER_DEGREE);
  int32_t sine = lv_trigo_sin(degrees);
  int32_t cosine = lv_trigo_sin((int16_t)((degrees + 90) % 360));
  int32_t phase = (int32_t)((snapshot->sim_time_ms / FLY_PHASE_MS) % 2U) * 2 - 1;
  int32_t gait = snapshot->animation == 1U ? phase * 2 : 0;
  int32_t wings = snapshot->animation == 2U ? 2 : 0;

  transform_point(&s_body_points[0], -6, 0, x, y, sine, cosine);
  transform_point(&s_body_points[1], 3, 0, x, y, sine, cosine);
  lv_line_set_points(s_body, s_body_points, 2);
  lv_point_precise_t head;
  transform_point(&head, 7, 0, x, y, sine, cosine);
  lv_obj_set_pos(s_head, (int32_t)head.x - 3, (int32_t)head.y - 3);

  for (size_t i = 0; i < FLY_LEG_COUNT; i++) {
    int32_t side = i < FLY_LEG_COUNT / 2U ? -1 : 1;
    int32_t pair = (int32_t)(i % (FLY_LEG_COUNT / 2U));
    int32_t root_x = 4 - pair * 4;
    int32_t tip_x = 10 - pair * 10 + (pair == 1 ? -gait : gait);
    if (snapshot->animation == 3U && pair == 0)
      tip_x = 11 + phase;
    transform_point(&s_leg_points[i][0], root_x, side * 2, x, y, sine, cosine);
    transform_point(&s_leg_points[i][1], root_x + 1, side * 7, x, y, sine, cosine);
    transform_point(&s_leg_points[i][2], tip_x, side * 12, x, y, sine, cosine);
    lv_line_set_points(s_legs[i], s_leg_points[i], FLY_LINE_POINTS);
  }
  for (size_t i = 0; i < FLY_WING_COUNT; i++) {
    int32_t side = i == 0 ? -1 : 1;
    transform_point(&s_wing_points[i][0], 1, side * 2, x, y, sine, cosine);
    transform_point(&s_wing_points[i][1], -9, side * (6 + wings), x, y, sine, cosine);
    transform_point(&s_wing_points[i][2], -5, side * 2, x, y, sine, cosine);
    lv_line_set_points(s_wings[i], s_wing_points[i], FLY_LINE_POINTS);
  }
  set_hidden(s_fly, false);
}

static void update_text(lv_obj_t *label, char *storage, size_t capacity, const char *text) {
  if (strcmp(storage, text) == 0)
    return;
  strlcpy(storage, text, capacity);
  lv_label_set_text_static(label, storage);
}

static void render_snapshot(const fly_link_snapshot_t *snapshot) {
  char text[FLY_TEXT_SIZE];
  uint32_t seconds = snapshot->sim_time_ms / FLY_MILLISECONDS_PER_SECOND;
  snprintf(text,
           sizeof(text),
           "%02lu:%02lu.%lu",
           (unsigned long)(seconds / FLY_SECONDS_PER_MINUTE),
           (unsigned long)(seconds % FLY_SECONDS_PER_MINUTE),
           (unsigned long)((snapshot->sim_time_ms % FLY_MILLISECONDS_PER_SECOND) / 100U));
  update_text(s_time, s_time_text, sizeof(s_time_text), text);
  snprintf(text,
           sizeof(text),
           "%u.%02ux",
           snapshot->speed_milli / FLY_LINK_SPEED_SCALE,
           (snapshot->speed_milli % FLY_LINK_SPEED_SCALE) / 10U);
  update_text(s_speed, s_speed_text, sizeof(s_speed_text), text);
  render_fly(snapshot);
  for (size_t i = 0; i < FLY_LINK_STIMULUS_MAX; i++) {
    bool visible = i < snapshot->stimulus_count;
    set_hidden(s_stimuli[i], !visible);
    if (!visible)
      continue;
    const fly_link_stimulus_t *stimulus = &snapshot->stimuli[i];
    int32_t x = coordinate_to_pixel(stimulus->x, s_arena_width);
    int32_t y = coordinate_to_pixel(stimulus->y, s_arena_height);
    lv_obj_set_pos(s_stimuli[i], x - 6, y - 6);
    lv_obj_set_style_border_color(
        s_stimuli[i],
        lv_color_hex(stimulus->kind == FLY_LINK_ACTION_SWEET ? FLY_COLOR_ACCENT : FLY_COLOR_LIGHT),
        0);
    lv_label_set_text_static(s_stimulus_labels[i],
                             stimulus->kind == FLY_LINK_ACTION_SWEET ? "S" : "T");
  }
  for (size_t i = 0; i < FLY_LINK_ACTIVITY_MAX; i++) {
    uint16_t activity = i < snapshot->activity_count ? snapshot->activity[i] : 0;
    lv_bar_set_value(s_bars[i], activity, LV_ANIM_OFF);
    if (i < snapshot->activity_count)
      snprintf(text, sizeof(text), "%u", activity / FLY_ACTIVITY_PERCENT_SCALE);
    else
      strlcpy(text, "--", sizeof(text));
    update_text(s_values[i], s_value_text[i], sizeof(s_value_text[i]), text);
  }
}

static void reset_presentation(void) {
  s_presented_sequence = 0;
  s_presented_frame = 0;
  if (s_visual_image != NULL)
    set_hidden(s_visual_image, true);
  s_notice = NULL;
  set_hidden(s_fly, true);
  for (size_t i = 0; i < FLY_LINK_STIMULUS_MAX; i++)
    set_hidden(s_stimuli[i], true);
  for (size_t i = 0; i < FLY_LINK_ACTIVITY_MAX; i++) {
    lv_bar_set_value(s_bars[i], 0, LV_ANIM_OFF);
    update_text(s_values[i], s_value_text[i], sizeof(s_value_text[i]), "--");
  }
  update_text(s_time, s_time_text, sizeof(s_time_text), "--:--.-");
  update_text(s_speed, s_speed_text, sizeof(s_speed_text), "--x");
}

static bool can_send(const fly_link_view_t *view) {
  return view->is_open && view->is_connected && view->has_snapshot &&
         view->snapshot.mode == view->mode && !view->is_stale && !view->has_overflow;
}

static void render_menu(void) {
  lv_label_set_text_static(s_menu_labels[FLY_MENU_STIMULUS],
                           s_is_local                                   ? "ANIMAR MOSCA"
                           : s_view.mode == FLY_LINK_MODE_VISUAL_3D     ? "INTERAGIR COM APARELHO"
                           : s_stimulus_action == FLY_LINK_ACTION_SWEET ? "Estimulo: DOCE"
                                                                        : "Estimulo: TOQUE");
  lv_label_set_text_static(
      s_menu_labels[FLY_MENU_PAUSE],
      (s_is_local ? s_local_state.paused : s_view.snapshot.is_paused) ? "RETOMAR" : "PAUSAR");
  lv_label_set_text_static(s_menu_labels[FLY_MENU_RESET],
                           s_is_local ? "REINICIAR CENA" : "REINICIAR TESTE");
  lv_label_set_text_static(s_menu_help,
                           s_is_local ? "Fora: setas camera / OK anima\nBACK fecha menu"
                                      : "OK escolher | BACK voltar");
  for (size_t i = 0; i < FLY_MENU_COUNT; i++) {
    lv_obj_set_style_bg_color(
        s_menu_rows[i], lv_color_hex(i == s_menu_index ? FLY_COLOR_SELECTED : FLY_COLOR_BG), 0);
    lv_obj_set_style_text_color(
        s_menu_labels[i], lv_color_hex(i == s_menu_index ? FLY_COLOR_LIGHT : FLY_COLOR_MUTED), 0);
  }
}

static void local_release(void) {
  // The timer and LVGL draw callbacks share the UI task. Detach before freeing.
  power_policy_set_display_hold(s_screen, false);
  if (s_local_image != NULL) {
    set_hidden(s_local_image, true);
    lv_image_set_src(s_local_image, NULL);
  }
  lv_image_cache_drop(&s_local_descriptor);
  fly_menu_texture_close();
  if (s_local_planes != NULL)
    heap_caps_free(s_local_planes);
  s_local_planes = NULL;
  memset(&s_local_descriptor, 0, sizeof(s_local_descriptor));
  s_local_tick = 0;
  s_local_dirty = true;
}

static void refresh_local(void) {
  if (s_local_planes == NULL && !s_local_memory_error) {
    // Two bounded planes (RGB565 + uint16 depth), PSRAM only; no per-frame heap.
    s_local_planes = heap_caps_malloc(FLY_LOCAL3D_PIXELS * sizeof(uint16_t) * 2U,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_local_planes == NULL)
      s_local_memory_error = true;
    else {
      memset(s_local_planes, 0, FLY_LOCAL3D_PIXELS * sizeof(uint16_t) * 2U);
      s_local_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
      s_local_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
      s_local_descriptor.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
      s_local_descriptor.header.w = FLY_LOCAL3D_WIDTH;
      s_local_descriptor.header.h = FLY_LOCAL3D_HEIGHT;
      s_local_descriptor.header.stride = FLY_LOCAL3D_WIDTH * 2U;
      s_local_descriptor.data_size = FLY_LOCAL3D_PIXELS * sizeof(uint16_t);
      s_local_descriptor.data = (const uint8_t *)s_local_planes;
      lv_image_set_src(s_local_image, &s_local_descriptor);
      // Read-only native menu preview: failure leaves the miniature LCD black.
      // Allocate/capture once on entry, never retry allocations every frame.
      if (!fly_menu_texture_open())
        ESP_LOGW(TAG, "Native menu texture unavailable");
      s_local_tick = lv_tick_get() - FLY_LOCAL3D_FRAME_MS;
      s_local_dirty = true;
    }
  }
  set_hidden(s_local_error, !s_local_memory_error);
  set_hidden(s_local_image, s_local_memory_error);
  power_policy_set_display_hold(s_screen, !s_local_memory_error && !s_local_state.paused);
  if (s_local_memory_error)
    return;
  uint32_t now = lv_tick_get();
  uint32_t elapsed = now - s_local_tick;
  if (elapsed < FLY_LOCAL3D_FRAME_MS)
    return;
  fly_local3d_advance(&s_local_state, elapsed);
  s_local_tick = now;
  if (s_local_state.paused && !s_local_dirty)
    return;
  if (!s_local_state.paused)
    fly_menu_texture_tick(elapsed);
  const fly_local3d_texture_t texture = {
      .pixels = fly_menu_texture_pixels(),
      .capacity_pixels = FLY_MENU_TEXTURE_PIXELS,
      .width = FLY_MENU_TEXTURE_WIDTH,
      .height = FLY_MENU_TEXTURE_HEIGHT,
      .stride_pixels = FLY_MENU_TEXTURE_WIDTH,
  };
  lv_image_cache_drop(&s_local_descriptor);
  if (!fly_local3d_render(&s_local_state,
                          &texture,
                          s_local_planes,
                          s_local_planes + FLY_LOCAL3D_PIXELS,
                          FLY_LOCAL3D_PIXELS,
                          FLY_LOCAL3D_WIDTH,
                          FLY_LOCAL3D_HEIGHT)) {
    s_local_memory_error = true;
    power_policy_set_display_hold(s_screen, false);
    set_hidden(s_local_image, true);
    set_hidden(s_local_error, false);
    return;
  }
  s_local_dirty = false;
  lv_obj_invalidate(s_local_image);
}

static void visual_release(void) {
  // UI task only: detach/invalidate LVGL's reference BEFORE freeing its pixels.
  if (s_visual_image != NULL) {
    set_hidden(s_visual_image, true);
    lv_image_set_src(s_visual_image, NULL);
  }
  lv_image_cache_drop(&s_visual_descriptor);
  if (s_visual_pixels != NULL)
    heap_caps_free(s_visual_pixels);
  s_visual_pixels = NULL;
  memset(&s_visual_descriptor, 0, sizeof(s_visual_descriptor));
  s_presented_frame = 0;
}

static void refresh_visual(const fly_link_view_t *view) {
  const char *state = "Aguardando imagem 3D do Mac";
  bool fresh = can_send(view) && view->has_frame && !view->is_frame_stale && !view->frame_error;
  if (s_visual_pixels == NULL && !s_visual_memory_error) {
    s_visual_pixels = heap_caps_malloc(FLY_LINK_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_visual_pixels == NULL) {
      s_visual_memory_error = true;
      fly_link_close(); // Tell the next Mac request that this UI cannot receive.
    } else {
      memset(s_visual_pixels, 0, FLY_LINK_FRAME_BYTES);
      s_visual_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
      s_visual_descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
      s_visual_descriptor.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
      s_visual_descriptor.header.w = FLY_LINK_FRAME_WIDTH;
      s_visual_descriptor.header.h = FLY_LINK_FRAME_HEIGHT;
      s_visual_descriptor.header.stride = FLY_LINK_FRAME_WIDTH * 2U;
      s_visual_descriptor.data_size = FLY_LINK_FRAME_BYTES;
      s_visual_descriptor.data = s_visual_pixels;
      lv_image_set_src(s_visual_image, &s_visual_descriptor);
    }
  }
  if (s_visual_memory_error) {
    fresh = false;
    state = "PSRAM insuficiente\nBACK para sair";
  } else if (view->frame_error)
    state = "Quadro recusado\nReconecte o Mac";
  else if (!view->is_connected)
    state = "Mac desconectado\nAguardando nova sessao";
  else if (view->has_overflow)
    state = "Fila cheia\nReconecte o Mac";
  else if (view->is_stale)
    state = "Sem dados recentes do Mac";
  else if (view->has_frame && view->is_frame_stale)
    state = "Imagem atrasada\nAguardando novo quadro";
  if (fresh && view->frame_sequence != s_presented_frame) {
    lv_image_cache_drop(&s_visual_descriptor);
    if (fly_link_frame_copy(
            view->generation, view->frame_sequence, s_visual_pixels, FLY_LINK_FRAME_BYTES) ==
        ESP_OK) {
      s_presented_frame = view->frame_sequence;
      lv_obj_invalidate(s_visual_image);
    } else
      fresh = false;
  }
  set_hidden(s_visual_image, !fresh);
  set_hidden(s_visual_state, fresh);
  // A healthy visual scene has no persistent headings or diagnostic text.
  set_hidden(s_visual_footer, true);
  lv_label_set_text_static(s_visual_state, state);
  lv_label_set_text_static(s_visual_footer,
                           fresh && view->snapshot.is_paused ? "PAUSADO | BACK para retomar"
                                                             : "Setas camera | OK acao | BACK");
}

static void refresh_view(void) {
  fly_link_view_t view;
  esp_err_t result = fly_link_get_view(&view);
  if (result != ESP_OK) {
    if (!s_link_error)
      ESP_LOGW(TAG, "View unavailable: %s", esp_err_to_name(result));
    s_link_error = true;
    s_is_local = true;
    set_hidden(s_legacy, true);
    set_hidden(s_visual, true);
    set_hidden(s_local, false);
    if (s_visual_pixels != NULL)
      visual_release();
    refresh_local();
    if (s_menu_open)
      render_menu();
    return;
  }
  s_link_error = false;
  s_view = view;
  bool visual = view.mode == FLY_LINK_MODE_VISUAL_3D;
  // Local animation is explicitly not neural. It needs no USB or host handshake.
  // Only a usable remote scene supersedes it; stale/disconnected links fall back.
  s_is_local = !can_send(&view) || (visual && (!view.has_frame || view.is_frame_stale ||
                                               view.frame_error || s_visual_memory_error));
  set_hidden(s_legacy, visual || s_is_local);
  set_hidden(s_visual, !visual || s_is_local);
  set_hidden(s_local, !s_is_local);
  if (view.generation != s_link_generation) {
    s_link_generation = view.generation;
    // Remote state may retain a stale snapshot, but disconnected presentation
    // falls back to the distinct non-neural local scene. CONFIG resets a session.
    if (visual || !(view.has_snapshot && view.is_stale && !view.is_connected))
      reset_presentation();
  }

  if (s_is_local) {
    if (s_visual_pixels != NULL)
      visual_release();
    refresh_local();
    if (s_menu_open)
      render_menu();
    return;
  }
  if (s_local_planes != NULL)
    local_release();

  if (visual) {
    refresh_visual(&view);
    if (s_menu_open)
      render_menu();
    return;
  }
  if (s_visual_pixels != NULL)
    visual_release();

  bool live = can_send(&view);
  if (live && view.snapshot.sequence != s_presented_sequence) {
    render_snapshot(&view.snapshot);
    s_presented_sequence = view.snapshot.sequence;
  }
  // This legacy remote branch follows host simulation time, never a local clock.
  // Unusable remote state selects the separate non-neural local scene above.
  const char *status = "Aguardando Mac";
  uint32_t color = FLY_COLOR_MUTED;
  if (view.has_overflow) {
    status = "Fila cheia | reconecte o Mac";
    color = FLY_COLOR_WARNING;
  } else if (view.is_stale && view.has_snapshot) {
    status = view.is_connected ? "Sem dados recentes | congelado" : "Mac desconectado | congelado";
    color = FLY_COLOR_WARNING;
  } else if (view.is_connected && !view.has_snapshot) {
    status = "Mac conectado | aguardando dados";
  } else if (live) {
    status = view.snapshot.is_paused ? "TESTE pausado pelo Mac" : "Mac conectado | TESTE";
    color = FLY_COLOR_ACCENT;
    if (s_notice != NULL && (int32_t)(s_notice_until - lv_tick_get()) > 0)
      status = s_notice;
  }
  lv_label_set_text_static(s_status, status);
  lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
  set_hidden(s_waiting, view.has_snapshot);
  char text[FLY_TEXT_SIZE];
  snprintf(text,
           sizeof(text),
           "OK %s | fila %u/%u",
           s_stimulus_action == FLY_LINK_ACTION_SWEET ? "Doce" : "Toque",
           (unsigned)view.queued_events,
           FLY_LINK_QUEUE_SIZE);
  update_text(s_action, s_action_text, sizeof(s_action_text), text);
  if (s_menu_open)
    render_menu();
}

static void timer_cb(lv_timer_t *timer) {
  uint32_t generation = (uint32_t)(uintptr_t)lv_timer_get_user_data(timer);
  if (timer != s_timer || generation != s_ui_generation)
    return;
  if (!s_is_open || s_screen == NULL || lv_screen_active() != s_screen) {
    ui_fly_stop();
    return;
  }
  refresh_view();
}

static void set_menu(bool open) {
  s_menu_open = open;
  set_hidden(s_menu, !open);
  if (open) {
    s_menu_index = FLY_MENU_STIMULUS;
    render_menu();
    lv_obj_move_foreground(s_menu);
  }
}

static void enqueue_action(fly_link_action_t action) {
  fly_link_view_t view;
  esp_err_t result = fly_link_get_view(&view);
  if (result != ESP_OK || view.generation != s_link_generation || !can_send(&view)) {
    s_notice = "Acao indisponivel | reconecte";
    s_notice_until = lv_tick_get() + FLY_NOTICE_MS;
    refresh_view();
    return;
  }
  result = fly_link_enqueue(action, s_cursor_x, s_cursor_y, 1U, view.generation);
  s_notice =
      result == ESP_OK ? "Acao na fila | aguardando Mac" : "Acao recusada | verifique o link";
  s_notice_until = lv_tick_get() + FLY_NOTICE_MS;
  if (result != ESP_OK)
    ESP_LOGW(TAG, "Queue action %u failed: %s", (unsigned)action, esp_err_to_name(result));
  refresh_view();
}

static uint16_t move_coordinate(uint16_t value, int32_t delta) {
  int32_t next = (int32_t)value + delta;
  if (next < 0)
    next = 0;
  if (next > FLY_LINK_COORD_MAX)
    next = FLY_LINK_COORD_MAX;
  return (uint16_t)next;
}

static void input_cb(const input_event_t *event, void *context) {
  if (event == NULL || (uint32_t)(uintptr_t)context != s_ui_generation)
    return;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool navigation = press || event->action == INPUT_ACTION_REPEAT;
  if (!s_is_ready) {
    if (press && event->button == INPUT_BTN_BACK)
      ui_switch_screen(SCREEN_GAMES_MENU);
    return;
  }
  if (press && event->button == INPUT_BTN_BACK) {
    set_menu(!s_menu_open);
    return;
  }
  if (s_menu_open) {
    if (navigation && event->button == INPUT_BTN_UP)
      s_menu_index = (s_menu_index + FLY_MENU_COUNT - 1U) % FLY_MENU_COUNT;
    else if (navigation && event->button == INPUT_BTN_DOWN)
      s_menu_index = (s_menu_index + 1U) % FLY_MENU_COUNT;
    else if ((press && event->button == INPUT_BTN_OK) ||
             (navigation && s_menu_index == FLY_MENU_STIMULUS &&
              (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT))) {
      switch (s_menu_index) {
        case FLY_MENU_STIMULUS:
          if (s_is_local) {
            fly_local3d_interact(&s_local_state);
            s_local_dirty = true;
            set_menu(false);
            break;
          }
          if (s_view.mode == FLY_LINK_MODE_VISUAL_3D) {
            enqueue_action(FLY_LINK_ACTION_SCROLL);
            set_menu(false);
            break;
          }
          s_stimulus_action = s_stimulus_action == FLY_LINK_ACTION_SWEET ? FLY_LINK_ACTION_TOUCH
                                                                         : FLY_LINK_ACTION_SWEET;
          break;
        case FLY_MENU_PAUSE:
          if (s_is_local) {
            s_local_state.paused = !s_local_state.paused;
            s_local_dirty = true;
          } else
            enqueue_action(FLY_LINK_ACTION_PAUSE);
          set_menu(false);
          break;
        case FLY_MENU_RESET:
          if (s_is_local) {
            fly_local3d_reset(&s_local_state);
            s_local_dirty = true;
          } else
            enqueue_action(FLY_LINK_ACTION_RESET);
          set_menu(false);
          break;
        case FLY_MENU_EXIT:
          ui_switch_screen(SCREEN_GAMES_MENU);
          return;
      }
    }
    render_menu();
    return;
  }
  if (press && event->button == INPUT_BTN_OK) {
    if (s_is_local) {
      fly_local3d_interact(&s_local_state);
      s_local_dirty = true;
      return;
    }
    enqueue_action(s_view.mode == FLY_LINK_MODE_VISUAL_3D ? FLY_LINK_ACTION_SCROLL
                                                          : s_stimulus_action);
    return;
  }
  if (!navigation)
    return;
  if (s_is_local) {
    int horizontal = event->button == INPUT_BTN_RIGHT  ? 1
                     : event->button == INPUT_BTN_LEFT ? -1
                                                       : 0;
    int vertical = event->button == INPUT_BTN_UP ? 1 : event->button == INPUT_BTN_DOWN ? -1 : 0;
    fly_local3d_camera(&s_local_state, horizontal, vertical);
    s_local_dirty = true;
    return;
  }
  switch (event->button) {
    case INPUT_BTN_UP:
      s_cursor_y = move_coordinate(s_cursor_y, -FLY_CURSOR_STEP);
      break;
    case INPUT_BTN_DOWN:
      s_cursor_y = move_coordinate(s_cursor_y, FLY_CURSOR_STEP);
      break;
    case INPUT_BTN_LEFT:
      s_cursor_x = move_coordinate(s_cursor_x, -FLY_CURSOR_STEP);
      break;
    case INPUT_BTN_RIGHT:
      s_cursor_x = move_coordinate(s_cursor_x, FLY_CURSOR_STEP);
      break;
    default:
      return;
  }
  if (s_view.mode == FLY_LINK_MODE_VISUAL_3D)
    enqueue_action(FLY_LINK_ACTION_CAMERA);
  else
    render_cursor();
}

static bool build_arena(int32_t width, int32_t height) {
  s_arena_width = width - FLY_MARGIN * 2;
  s_arena_height = height - FLY_ARENA_TOP - FLY_BOTTOM_SPACE;
  if (s_arena_width < FLY_ARENA_INSET * 3 || s_arena_height < FLY_ARENA_INSET * 3)
    return false;
  s_arena =
      make_box(s_legacy, FLY_MARGIN, FLY_ARENA_TOP, s_arena_width, s_arena_height, FLY_COLOR_BG);
  if (s_arena == NULL)
    return false;
  lv_obj_set_style_border_width(s_arena, 1, 0);
  lv_obj_set_style_border_color(s_arena, lv_color_hex(FLY_COLOR_GRID), 0);
  s_fly = make_box(s_arena, 0, 0, s_arena_width, s_arena_height, FLY_COLOR_BG);
  if (s_fly == NULL)
    return false;
  lv_obj_set_style_bg_opa(s_fly, LV_OPA_TRANSP, 0);
  for (size_t i = 0; i < FLY_LEG_COUNT; i++) {
    s_legs[i] = make_line(s_fly, FLY_COLOR_MUTED, 1);
    if (s_legs[i] == NULL)
      return false;
  }
  for (size_t i = 0; i < FLY_WING_COUNT; i++) {
    s_wings[i] = make_line(s_fly, FLY_COLOR_LIGHT, 3);
    if (s_wings[i] == NULL)
      return false;
    lv_obj_set_style_line_opa(s_wings[i], LV_OPA_50, 0);
  }
  s_body = make_line(s_fly, FLY_COLOR_ACCENT, 7);
  s_head = make_box(s_fly, 0, 0, 6, 6, FLY_COLOR_LIGHT);
  if (s_body == NULL || s_head == NULL)
    return false;
  lv_obj_set_style_radius(s_head, LV_RADIUS_CIRCLE, 0);
  for (size_t i = 0; i < FLY_LINK_STIMULUS_MAX; i++) {
    s_stimuli[i] = make_box(s_arena, 0, 0, 13, 13, FLY_COLOR_BG);
    if (s_stimuli[i] == NULL)
      return false;
    lv_obj_set_style_border_width(s_stimuli[i], 1, 0);
    lv_obj_set_style_radius(s_stimuli[i], LV_RADIUS_CIRCLE, 0);
    s_stimulus_labels[i] = make_label(s_stimuli[i], 3, 0, 9, "S", FLY_COLOR_LIGHT);
    if (s_stimulus_labels[i] == NULL)
      return false;
  }
  s_cursor[0] = make_box(s_arena, 0, 0, FLY_CURSOR_HALF * 2 + 1, 1, FLY_COLOR_ACCENT);
  s_cursor[1] = make_box(s_arena, 0, 0, 1, FLY_CURSOR_HALF * 2 + 1, FLY_COLOR_ACCENT);
  s_waiting = make_label(s_arena,
                         4,
                         0,
                         s_arena_width - 8,
                         "Aguardando Mac\nAtive USB NATIVE\nInicie companion no Mac",
                         FLY_COLOR_MUTED);
  if (s_cursor[0] == NULL || s_cursor[1] == NULL || s_waiting == NULL)
    return false;
  lv_obj_set_style_text_align(s_waiting, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(s_waiting);
  render_cursor();
  return true;
}

static bool build_activity(int32_t width, int32_t height) {
  int32_t top = height - FLY_BOTTOM_SPACE + 4;
  if (make_label(s_legacy,
                 FLY_MARGIN,
                 top,
                 width - FLY_MARGIN * 2,
                 "ATIVIDADE TESTE / A1-A4 (%)",
                 FLY_COLOR_MUTED) == NULL)
    return false;
  int32_t column = (width - FLY_MARGIN * 2) / 2;
  for (size_t i = 0; i < FLY_LINK_ACTIVITY_MAX; i++) {
    int32_t x = FLY_MARGIN + (int32_t)(i % 2U) * column;
    int32_t y = top + 17 + (int32_t)(i / 2U) * 15;
    static const char *const names[] = {"A1", "A2", "A3", "A4"};
    if (make_label(s_legacy, x, y - 2, 18, names[i], FLY_COLOR_MUTED) == NULL)
      return false;
    s_bars[i] = lv_bar_create(s_legacy);
    if (s_bars[i] == NULL)
      return false;
    lv_obj_set_pos(s_bars[i], x + 20, y);
    lv_obj_set_size(s_bars[i], column - 52, 8);
    lv_bar_set_range(s_bars[i], 0, FLY_LINK_ACTIVITY_SCALE);
    lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(FLY_COLOR_GRID), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(FLY_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bars[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bars[i], 0, LV_PART_INDICATOR);
    s_values[i] = make_label(s_legacy, x + column - 28, y - 2, 27, "--", FLY_COLOR_LIGHT);
    if (s_values[i] == NULL)
      return false;
  }
  return true;
}

static bool build_menu(int32_t width) {
  s_menu = make_box(s_screen, 0, 0, width - 32, FLY_MENU_HEIGHT, FLY_COLOR_BG);
  if (s_menu == NULL)
    return false;
  lv_obj_set_style_border_width(s_menu, 1, 0);
  lv_obj_set_style_border_color(s_menu, lv_color_hex(FLY_COLOR_ACCENT), 0);
  lv_obj_set_style_radius(s_menu, 8, 0);
  lv_obj_center(s_menu);
  if (make_label(s_menu, 12, 9, width - 56, "MOSCA / ACOES", FLY_COLOR_LIGHT) == NULL)
    return false;
  for (size_t i = 0; i < FLY_MENU_COUNT; i++) {
    s_menu_rows[i] = make_box(s_menu,
                              6,
                              FLY_MENU_ROW_TOP + (int32_t)i * FLY_MENU_ROW_HEIGHT,
                              width - 44,
                              FLY_MENU_ROW_HEIGHT - 2,
                              FLY_COLOR_BG);
    if (s_menu_rows[i] == NULL)
      return false;
    s_menu_labels[i] = make_label(s_menu_rows[i], 8, 6, width - 60, "", FLY_COLOR_LIGHT);
    if (s_menu_labels[i] == NULL)
      return false;
  }
  lv_label_set_text_static(s_menu_labels[FLY_MENU_RESET], "REINICIAR TESTE");
  lv_label_set_text_static(s_menu_labels[FLY_MENU_EXIT], "SAIR");
  s_menu_help = make_label(
      s_menu, 12, FLY_MENU_HEIGHT - 37, width - 56, "OK escolher | BACK voltar", FLY_COLOR_MUTED);
  if (s_menu_help == NULL)
    return false;
  set_hidden(s_menu, true);
  return true;
}

static bool build_visual(int32_t width, int32_t height) {
  if (width < (int32_t)FLY_LINK_FRAME_WIDTH || height < 240)
    return false;
  s_visual = make_box(s_screen, 0, 0, width, height, FLY_COLOR_BG);
  if (s_visual == NULL)
    return false;
  set_hidden(s_visual, true); // Allocation failure must not obscure the error UI.
  s_visual_image = lv_image_create(s_visual);
  if (s_visual_image == NULL)
    return false;
  lv_obj_set_pos(s_visual_image,
                 (width - (int32_t)FLY_LINK_FRAME_WIDTH) / 2,
                 (height - (int32_t)FLY_LINK_FRAME_HEIGHT) / 2);
  lv_obj_set_size(s_visual_image, FLY_LINK_FRAME_WIDTH, FLY_LINK_FRAME_HEIGHT);
  lv_obj_remove_flag(s_visual_image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  set_hidden(s_visual_image, true);
  s_visual_state = make_label(
      s_visual, 4, height / 2 - 20, width - 8, "Aguardando imagem 3D do Mac", FLY_COLOR_MUTED);
  s_visual_footer = make_label(
      s_visual, 4, height - 14, width - 8, "Setas camera | OK acao | BACK", FLY_COLOR_MUTED);
  if (s_visual_state == NULL || s_visual_footer == NULL)
    return false;
  lv_obj_set_style_text_align(s_visual_state, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(s_visual_footer, LV_TEXT_ALIGN_CENTER, 0);
  set_hidden(s_visual, true);
  return true;
}

static bool build_local(int32_t width, int32_t height) {
  if (width < (int32_t)FLY_LOCAL3D_WIDTH || height < (int32_t)FLY_LOCAL3D_HEIGHT)
    return false;
  s_local = make_box(s_screen, 0, 0, width, height, FLY_COLOR_BG);
  if (s_local == NULL)
    return false;
  set_hidden(s_local, true); // A later menu/timer failure must not cover its error UI.
  s_local_image = lv_image_create(s_local);
  s_local_error = make_label(s_local,
                             8,
                             height / 2 - 24,
                             width - 16,
                             "Cena indisponivel\nBACK: acoes / sair",
                             FLY_COLOR_MUTED);
  if (s_local_image == NULL || s_local_error == NULL)
    return false;
  lv_obj_set_size(s_local_image, FLY_LOCAL3D_WIDTH, FLY_LOCAL3D_HEIGHT);
  lv_obj_set_pos(s_local_image,
                 (width - (int32_t)FLY_LOCAL3D_WIDTH) / 2,
                 (height - (int32_t)FLY_LOCAL3D_HEIGHT) / 2);
  lv_obj_remove_flag(s_local_image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_align(s_local_error, LV_TEXT_ALIGN_CENTER, 0);
  set_hidden(s_local_image, true);
  set_hidden(s_local_error, true);
  return true;
}

void ui_fly_stop(void) {
  s_ui_generation++;
  if (s_ui_generation == 0)
    s_ui_generation++;
  if (s_timer != NULL) {
    lv_timer_t *timer = s_timer;
    s_timer = NULL;
    lv_timer_delete(timer);
  }
  if (s_is_open) {
    s_is_open = false;
    visual_release();
    local_release();
    fly_link_close();
    ui_input_set_screen_handler(NULL, NULL);
    ESP_LOGI(TAG, "Mosca view stopped");
  }
  s_is_ready = false;
}

static void screen_delete_cb(lv_event_t *event) {
  if (lv_event_get_target_obj(event) != s_screen)
    return;
  ui_fly_stop();
  s_screen = NULL;
  s_visual_image = NULL;
  s_visual = NULL;
  s_legacy = NULL;
  s_local_image = NULL;
  s_local_error = NULL;
  s_local = NULL;
}

void ui_fly_open(void) {
  ui_fly_stop();
  if (s_screen != NULL)
    lv_obj_delete(s_screen);
  s_screen = lv_obj_create(NULL);
  if (s_screen == NULL) {
    ESP_LOGE(TAG, "Unable to allocate Mosca screen");
    ui_input_set_screen_handler(input_cb, (void *)(uintptr_t)s_ui_generation);
    return;
  }
  lv_obj_remove_style_all(s_screen);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(FLY_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  if (lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL) == NULL) {
    lv_obj_delete(s_screen);
    s_screen = NULL;
    ESP_LOGE(TAG, "Unable to register Mosca screen cleanup");
    ui_input_set_screen_handler(input_cb, (void *)(uintptr_t)s_ui_generation);
    return;
  }
  int32_t width = ui_screen_w();
  int32_t height = ui_screen_h();
  s_legacy = make_box(s_screen, 0, 0, width, height, FLY_COLOR_BG);
  if (s_legacy == NULL) {
    ESP_LOGE(TAG, "Unable to allocate Mosca layout");
    ui_input_set_screen_handler(input_cb, (void *)(uintptr_t)s_ui_generation);
    ui_screen_load(s_screen);
    return;
  }
  memset(&s_view, 0, sizeof(s_view));
  memset(s_time_text, 0, sizeof(s_time_text));
  memset(s_speed_text, 0, sizeof(s_speed_text));
  memset(s_action_text, 0, sizeof(s_action_text));
  memset(s_value_text, 0, sizeof(s_value_text));
  s_cursor_x = FLY_CURSOR_START;
  s_cursor_y = FLY_CURSOR_START;
  s_stimulus_action = FLY_LINK_ACTION_SWEET;
  s_menu_open = false;
  s_link_error = false;
  s_link_generation = UINT32_MAX;
  s_visual_memory_error = false;
  s_local_memory_error = false;
  s_is_local = true;
  s_local_dirty = true;
  fly_local3d_reset(&s_local_state);
  s_presented_frame = 0;

  lv_obj_t *title = make_label(s_legacy, FLY_MARGIN, 6, 90, "MOSCA", FLY_COLOR_LIGHT);
  if (title != NULL)
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  s_time = make_label(s_legacy, width - 110, 7, 102, "--:--.-", FLY_COLOR_LIGHT);
  s_speed = make_label(s_legacy, width - 62, 27, 54, "--x", FLY_COLOR_ACCENT);
  s_status = make_label(
      s_legacy, FLY_MARGIN, 44, width - FLY_MARGIN * 2, "Aguardando Mac", FLY_COLOR_MUTED);
  s_action = make_label(s_legacy,
                        FLY_MARGIN,
                        height - 31,
                        width - FLY_MARGIN * 2,
                        "OK Doce | fila 0/16",
                        FLY_COLOR_ACCENT);
  bool labels_ok =
      title != NULL && s_time != NULL && s_speed != NULL && s_status != NULL && s_action != NULL;
  labels_ok =
      labels_ok &&
      make_label(s_legacy, FLY_MARGIN, 27, width - 75, "TESTE / nao neural", FLY_COLOR_MUTED) !=
          NULL;
  labels_ok = labels_ok && make_label(s_legacy,
                                      FLY_MARGIN,
                                      height - 15,
                                      width - FLY_MARGIN * 2,
                                      "Setas cursor | BACK acoes",
                                      FLY_COLOR_MUTED) != NULL;
  s_is_ready = labels_ok && build_arena(width, height) && build_activity(width, height) &&
               build_visual(width, height) && build_local(width, height) && build_menu(width);
  if (s_is_ready) {
    reset_presentation();
    if (fly_link_open() != ESP_OK) {
      // Local mode remains available if remote synchronization cannot initialize.
      ESP_LOGW(TAG, "Mac link unavailable; continuing with local 3D");
    }
    s_is_open = true;
    s_timer = lv_timer_create(timer_cb, FLY_TICK_MS, (void *)(uintptr_t)s_ui_generation);
    if (s_timer == NULL) {
      ui_fly_stop();
      ESP_LOGE(TAG, "Unable to allocate Mosca timer");
    }
  }
  if (!s_is_ready) {
    ESP_LOGE(TAG, "Mosca view unavailable: allocation or display size");
    if (s_local != NULL)
      set_hidden(s_local, true);
    if (s_visual != NULL)
      set_hidden(s_visual, true);
    if (s_legacy != NULL)
      set_hidden(s_legacy, false);
    if (s_status != NULL)
      lv_label_set_text_static(s_status, "Indisponivel | BACK para sair");
  }
  ui_input_set_screen_handler(input_cb, (void *)(uintptr_t)s_ui_generation);
  ui_screen_load(s_screen);
  if (s_is_ready)
    refresh_view();
  ESP_LOGI(TAG, "Mosca local 3D view opened (%ld x %ld)", (long)width, (long)height);
}
