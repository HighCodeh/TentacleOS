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

// UI subsystem of the app ABI (`api->ui->...`): lets a .hb app own a full-screen
// LVGL UI on the ST7789 without ever seeing an LVGL type. Apps get opaque,
// generation-tagged handles into a firmware-owned, per-task widget table; every
// call runs on the app task and is bracketed by the (recursive) LVGL lock. Input
// arrives through a small queue the app drains with poll_event, so no app code
// ever runs on the LVGL task. On app exit or force-kill, tos_ui_app_teardown()
// (called from the manager's finish_slot) deletes the screen and invalidates the
// handles. Because strings are copied and no app pointer is ever handed to LVGL,
// the widget tree can be torn down safely even after the app's ELF is unloaded.

#include "tos_api.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#include "input_manager.h"
#include "lvgl_glue.h"
#include "resource_mgr.h"
#include "st7789.h"
#include "tos_api_ui.h"
#include "tos_app_ctx.h"
#include "ui_manager.h"

#define NEED(cap)                   \
  do {                              \
    if (!tos_app_cap_check(cap))    \
      return ESP_ERR_NOT_SUPPORTED; \
  } while (0)

#define UI_SESSIONS 4  // matches TOS_APP_MAX
#define UI_MAX_OBJS 48 // per-app widget table size
#define UI_EVQ_LEN  8  // input ring depth
#define UI_SCREEN_W 240
#define UI_SCREEN_H 320
#define SPI3_CANVAS_FLUSH_MS 50 // per-strip DMA wait for canvas blits
#define UI_IMG_HDR           8   // app image .bin header: magic_cf u32 + w u16 + h u16

enum { UI_T_LABEL = 0, UI_T_RECT, UI_T_BAR, UI_T_IMAGE };

typedef struct {
  lv_obj_t *obj;
  uint16_t gen;
  uint8_t type;
  bool used;
} ui_wentry_t;

typedef struct {
  void *task;         // owning app task (key)
  lv_obj_t *screen;   // firmware-owned root, or NULL
  ui_wentry_t w[UI_MAX_OBJS];
  QueueHandle_t evq;
  res_handle_t display; // RES_DISPLAY lease held while the app's screen is up
  bool used;
  bool canvas; // direct-panel (canvas) mode vs widget mode; mutually exclusive
} ui_session_t;

static ui_session_t s_sessions[UI_SESSIONS];

// Canvas mode: one shared DMA strip buffer (single-owner display), reused across
// blits AND across successive canvas apps. Reused single-buffer + wait_flush per
// strip is the proven Game Boy path. Allocated lazily on the first blit and kept
// for the process lifetime (never freed — a strip's DMA may outlive a blit whose
// wait_flush timed out, so freeing it on teardown could be read-after-free).
#define CANVAS_STRIP_ROWS 20     // upper bound; the real size adapts to free DMA RAM
static uint16_t *s_canvas_strip; // UI_SCREEN_W * s_canvas_strip_rows px, DMA-capable
static int s_canvas_strip_rows;  // rows that actually fit internal DMA RAM (>=1)

// --- session lookup (all callers hold, or are serialized by, the LVGL lock) ---

static ui_session_t *session_for(void *task) {
  for (int i = 0; i < UI_SESSIONS; i++)
    if (s_sessions[i].used && s_sessions[i].task == task)
      return &s_sessions[i];
  return NULL;
}
static ui_session_t *session_self(void) {
  return session_for(xTaskGetCurrentTaskHandle());
}

// --- handle table: handle = (gen<<16) | (index+1); 0 == none ------------------

static tos_ui_obj_t make_handle(int idx, uint16_t gen) {
  return ((uint32_t)gen << 16) | (uint32_t)(idx + 1);
}

// Resolve a handle to a live LVGL object, or NULL. want_type < 0 accepts any.
// Validates the generation AND that the object is still alive, so a handle from a
// torn-down screen or a deleted parent's child resolves to nothing (safe no-op).
static lv_obj_t *resolve_t(ui_session_t *s, tos_ui_obj_t h, int want_type) {
  if (s == NULL || h == TOS_UI_NONE)
    return NULL;
  int idx = (int)(h & 0xFFFF) - 1;
  uint16_t gen = (uint16_t)(h >> 16);
  if (idx < 0 || idx >= UI_MAX_OBJS)
    return NULL;
  ui_wentry_t *w = &s->w[idx];
  if (!w->used || w->gen != gen || w->obj == NULL)
    return NULL;
  if (!lv_obj_is_valid(w->obj)) { // parent deleted it out from under us
    w->used = false;
    w->gen++;
    w->obj = NULL;
    return NULL;
  }
  if (want_type >= 0 && w->type != (uint8_t)want_type)
    return NULL;
  return w->obj;
}
static lv_obj_t *resolve(ui_session_t *s, tos_ui_obj_t h) {
  return resolve_t(s, h, -1);
}

static tos_ui_obj_t track(ui_session_t *s, lv_obj_t *obj, uint8_t type) {
  if (obj == NULL)
    return TOS_UI_NONE;
  for (int i = 0; i < UI_MAX_OBJS; i++) {
    if (!s->w[i].used) {
      s->w[i].used = true;
      s->w[i].obj = obj;
      s->w[i].type = type;
      return make_handle(i, s->w[i].gen);
    }
  }
  return TOS_UI_NONE; // table full
}

static lv_obj_t *parent_obj(ui_session_t *s, tos_ui_obj_t parent) {
  if (parent == TOS_UI_NONE)
    return s->screen;
  lv_obj_t *p = resolve(s, parent);
  return p ? p : s->screen;
}

static lv_color_t rgb(tos_rgb_t c) {
  return lv_color_hex(c & 0x00FFFFFF);
}

static void invalidate_all(ui_session_t *s) {
  for (int i = 0; i < UI_MAX_OBJS; i++) {
    s->w[i].used = false;
    s->w[i].gen++;
    s->w[i].obj = NULL;
  }
}

// Screen deleted (e.g. by ui_switch_screen's del_async on teardown): drop it and
// invalidate the handles — but only if it is still THIS session's screen, so a
// stale delete cannot clobber a session slot that was reused by a new app.
static void screen_deleted_cb(lv_event_t *e) {
  ui_session_t *s = (ui_session_t *)lv_event_get_user_data(e);
  if (s != NULL && s->screen == lv_event_get_target(e)) {
    s->screen = NULL;
    invalidate_all(s);
  }
}

// --- input: firmware trampoline on the UI task -> queue drained by the app -----

static uint8_t map_key(uint8_t btn) {
  switch (btn) {
    case INPUT_BTN_UP:
      return TOS_KEY_UP;
    case INPUT_BTN_DOWN:
      return TOS_KEY_DOWN;
    case INPUT_BTN_LEFT:
      return TOS_KEY_LEFT;
    case INPUT_BTN_RIGHT:
      return TOS_KEY_RIGHT;
    case INPUT_BTN_OK:
      return TOS_KEY_OK;
    case INPUT_BTN_BACK:
      return TOS_KEY_BACK;
    default:
      return 0;
  }
}
// Returns 0xFF for actions the app does not receive (LONG_PRESS is redundant
// with the PRESS -> REPEAT stream the app already gets).
static uint8_t map_action(uint8_t action) {
  switch (action) {
    case INPUT_ACTION_PRESS:
      return TOS_KEY_PRESS;
    case INPUT_ACTION_REPEAT:
      return TOS_KEY_REPEAT;
    case INPUT_ACTION_RELEASE:
      return TOS_KEY_RELEASE;
    default:
      return 0xFF;
  }
}
static void app_input_trampoline(const input_event_t *ev, void *ctx) {
  ui_session_t *s = (ui_session_t *)ctx;
  if (s == NULL || s->evq == NULL)
    return;
  uint8_t key = map_key(ev->button);
  uint8_t action = map_action(ev->action);
  if (key == 0 || action == 0xFF)
    return;
  tos_ui_event_t out = {.key = key, .action = action};
  if (xQueueSend(s->evq, &out, 0) != pdTRUE) { // full: drop oldest, then enqueue
    tos_ui_event_t dump;
    (void)xQueueReceive(s->evq, &dump, 0);
    (void)xQueueSend(s->evq, &out, 0);
  }
}

// --- lifecycle ----------------------------------------------------------------

// RES_DISPLAY was preempted (a native UI screen returned to the foreground). Tell
// the app via the same resource_lost() flag it already polls for radios; its loop
// breaks, it returns, and finish_slot reclaims everything. The app's screen has
// already been replaced by the preemptor, so there is nothing to dismiss here.
static void ui_display_app_revoked(void *user) {
  tos_app_ctx_signal_resource_lost(user); // user == the app task handle
}

// Shared body for screen_open (widget mode) and canvas_begin (direct mode). Both
// take over the panel with a black SCREEN_APP_RUNNING screen and lease RES_DISPLAY;
// they differ only in the session's mode flag (which gates the widget builders vs
// canvas_blit). The black LVGL screen underneath a canvas session is normally clean,
// so the render task does not stomp the app's direct blits between frames — but a
// system overlay on the top layer (e.g. a notification toast) can still repaint over
// the canvas; a full-frame canvas app overwrites it on the next blit.
static esp_err_t open_display(bool canvas) {
  NEED(TOS_CAP_UI);
  if (!lvgl_glue_is_ready())
    return ESP_ERR_INVALID_STATE;
  void *task = xTaskGetCurrentTaskHandle();
  if (!ui_acquire())
    return ESP_ERR_TIMEOUT;

  ui_session_t *s = session_for(task);
  if (s != NULL && s->screen != NULL) { // this app already has a screen
    ui_release();
    return ESP_OK;
  }
  // Single-owner: refuse if another app already holds the display.
  for (int i = 0; i < UI_SESSIONS; i++) {
    if (s_sessions[i].used && s_sessions[i].screen != NULL && s_sessions[i].task != task) {
      ui_release();
      return ESP_ERR_INVALID_STATE;
    }
  }
  if (s == NULL) {
    for (int i = 0; i < UI_SESSIONS; i++)
      if (!s_sessions[i].used) {
        s = &s_sessions[i];
        break;
      }
  }
  if (s == NULL) {
    ui_release();
    return ESP_ERR_NO_MEM;
  }

  // Clear used/obj but preserve gen counters so handles keep monotonically aging.
  for (int i = 0; i < UI_MAX_OBJS; i++) {
    s->w[i].used = false;
    s->w[i].obj = NULL;
  }
  if (s->evq == NULL)
    s->evq = xQueueCreate(UI_EVQ_LEN, sizeof(tos_ui_event_t));
  else
    xQueueReset(s->evq);
  if (s->evq == NULL) {
    ui_release();
    return ESP_ERR_NO_MEM;
  }
  s->task = task;
  s->used = true;
  s->canvas = canvas;
  s->display = RES_HANDLE_NONE;

  lv_obj_t *scr = lv_obj_create(NULL);
  if (scr == NULL) {
    s->used = false;
    s->task = NULL;
    ui_release();
    return ESP_ERR_NO_MEM;
  }
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, screen_deleted_cb, LV_EVENT_DELETE, s);
  s->screen = scr;

  ui_app_screen_enter(scr, app_input_trampoline, s); // releases the UI's display grant

  // Lease the panel for the app (prio 10). ui_app_screen_enter just freed it, and
  // the LVGL lock we hold keeps any other app's screen_open out until we return,
  // so this cannot race. A failure is non-fatal: the screen is up and works, it
  // just won't get clean preemption — the widget invalidation path still applies.
  res_request_t req = {
      .id = RES_DISPLAY,
      .lane = RES_LANE_MAIN,
      .owner_kind = RES_OWNER_APP,
      .owner_task = task,
      .priority = 0, // default: app = 10
      .allow_preempt = false,
      .on_revoke = ui_display_app_revoked,
      .user = task,
  };
  if (resource_acquire(&req, &s->display) != ESP_OK)
    s->display = RES_HANDLE_NONE;

  ui_release();
  return ESP_OK;
}

static esp_err_t u_screen_open(void) {
  return open_display(false);
}

void tos_ui_app_teardown(void *task) {
  ui_session_t *s = session_for(task);
  if (s == NULL || !s->used)
    return;

  // Hold the lock across the whole teardown so the "still foreground?" check and
  // the switch-back are atomic: another UI app starting concurrently can't slip
  // its new screen in between and have us clobber it. ui_switch_screen re-takes
  // the (recursive) lock harmlessly. If the lock is unreachable (should not happen
  // from app/killer context), free the slot without touching LVGL and let the next
  // navigation reclaim the orphaned screen.
  if (!ui_acquire()) {
    s->task = NULL;
    s->used = false;
    return;
  }
  resource_release(s->display); // drop the panel lease (backstop: finish_slot also releases by task)
  s->display = RES_HANDLE_NONE;
  if (s->canvas) {
    lvgl_glue_direct_end(); // defensive: each blit already pairs begin/end under the lock
    s->canvas = false;
    // s_canvas_strip is intentionally NOT freed here: a strip's DMA can still be in
    // flight if the last blit's wait_flush timed out, and freeing it would let the
    // controller read freed memory. It is a small bounded scratch buffer reused by
    // the next canvas app, so it lives for the process lifetime.
  }
  lv_obj_t *scr = s->screen;
  if (scr != NULL)
    ui_input_set_screen_handler(NULL, NULL); // stop the trampoline before we tear down
  invalidate_all(s);
  s->screen = NULL;
  if (s->evq != NULL) {
    vQueueDelete(s->evq);
    s->evq = NULL;
  }
  s->task = NULL;
  s->used = false;
  if (scr != NULL) {
    if (ui_current_screen() == SCREEN_APP_RUNNING) {
      ui_switch_screen(SCREEN_GAMES_MENU); // back to the Apps carousel; del_async's the app screen
    } else if (lv_obj_is_valid(scr) && scr == lv_screen_active()) {
      // Non-foreground: if scr is still the active screen, nobody replaced it, so
      // nobody scheduled its deletion — delete it now. If it is NOT the active
      // screen, whoever switched away (ui_switch_screen / another app's
      // ui_app_screen_enter) already del_async'd it; deleting again would double-free.
      lv_obj_del_async(scr);
    }
  }
  ui_release();
}

static void u_screen_close(void) {
  tos_ui_app_teardown(xTaskGetCurrentTaskHandle());
}

static void u_metrics(int32_t *w, int32_t *h) {
  if (w != NULL)
    *w = UI_SCREEN_W;
  if (h != NULL)
    *h = UI_SCREEN_H;
}

// --- canvas mode (direct panel) -----------------------------------------------

static esp_err_t u_canvas_begin(int32_t *w, int32_t *h) {
  esp_err_t e = open_display(true);
  if (e != ESP_OK)
    return e;
  if (w != NULL)
    *w = UI_SCREEN_W;
  if (h != NULL)
    *h = UI_SCREEN_H;
  return ESP_OK;
}

// Blit a little-endian RGB565 rect to the panel. The panel wants big-endian on the
// wire (LVGL's own path sets swap_bytes), so we byte-swap into a DMA strip buffer a
// few rows at a time and wait for each strip's DMA before reusing the buffer. Runs
// under the LVGL lock in direct mode, so the render task can't drive the panel or
// the SPI bus concurrently; the lock is released at return, keeping the app killable
// between frames.
static esp_err_t
u_canvas_blit(const void *src, int32_t x, int32_t y, int32_t w, int32_t h) {
  NEED(TOS_CAP_UI);
  // Overflow-safe clamp: each dimension must fit the panel, then the origin must
  // leave room. w<=UI_SCREEN_W makes (UI_SCREEN_W - w) >= 0, so no wrap in x/y.
  if (src == NULL || w <= 0 || h <= 0 || w > UI_SCREEN_W || h > UI_SCREEN_H || x < 0 || y < 0 ||
      x > UI_SCREEN_W - w || y > UI_SCREEN_H - h)
    return ESP_ERR_INVALID_ARG;
  ui_session_t *s = session_self();
  if (s == NULL || !s->canvas)
    return ESP_ERR_INVALID_STATE;
  // Refuse to touch the panel once the display lease is gone (preempted by a native
  // UI screen). The check is synchronous — preemption reused the grant slot with a
  // new token — so it closes the window before the app's next resource_lost() poll.
  if (!resource_handle_valid(s->display))
    return ESP_ERR_INVALID_STATE;
  if (!ui_acquire())
    return ESP_ERR_TIMEOUT;
  if (s_canvas_strip == NULL) {
    // Internal DMA RAM is scarce and fragmented on this board (LVGL's draw buffers
    // already hold a large internal chunk), so a big contiguous strip may not fit —
    // even the Game Boy's 15 KB strip can fail here. Take the largest strip that
    // fits, halving down to a single row (480 B), which almost always succeeds.
    int start = CANVAS_STRIP_ROWS;
    // Size the first attempt to the largest free DMA block so we don't trip the
    // heap's noisy "alloc failed" log before falling back to a smaller strip.
    size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    while (start > 1 && (size_t)UI_SCREEN_W * start * sizeof(uint16_t) > avail)
      start >>= 1;
    for (int rows = start; rows >= 1 && s_canvas_strip == NULL; rows >>= 1) {
      s_canvas_strip = heap_caps_malloc((size_t)UI_SCREEN_W * rows * sizeof(uint16_t),
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
      if (s_canvas_strip != NULL)
        s_canvas_strip_rows = rows;
    }
  }
  if (s_canvas_strip == NULL) {
    ui_release();
    return ESP_ERR_NO_MEM;
  }

  const int32_t srows = s_canvas_strip_rows;
  const uint16_t *px = (const uint16_t *)src;
  esp_err_t ret = ESP_OK;
  lvgl_glue_direct_begin();
  for (int32_t row = 0; row < h && ret == ESP_OK; row += srows) {
    int32_t rows = (row + srows <= h) ? srows : (h - row);
    for (int32_t r = 0; r < rows; r++) {
      const uint16_t *in = px + (size_t)(row + r) * (size_t)w;
      uint16_t *out = s_canvas_strip + (size_t)r * (size_t)w;
      for (int32_t c = 0; c < w; c++)
        out[c] = (uint16_t)((in[c] >> 8) | (in[c] << 8));
    }
    ret = esp_lcd_panel_draw_bitmap(panel_handle, x, y + row, x + w, y + row + rows, s_canvas_strip);
    lvgl_glue_wait_flush(SPI3_CANVAS_FLUSH_MS);
  }
  lvgl_glue_direct_end();
  ui_release();
  return ret;
}

static void u_canvas_end(void) {
  tos_ui_app_teardown(xTaskGetCurrentTaskHandle());
}

// --- builders -----------------------------------------------------------------

static tos_ui_obj_t u_label(tos_ui_obj_t parent, int32_t x, int32_t y, const char *text) {
  if (!tos_app_cap_check(TOS_CAP_UI))
    return TOS_UI_NONE;
  ui_session_t *s = session_self();
  if (s == NULL || s->screen == NULL || s->canvas || !ui_acquire())
    return TOS_UI_NONE;
  lv_obj_t *o = lv_label_create(parent_obj(s, parent));
  if (o != NULL) {
    lv_label_set_text(o, text ? text : ""); // copies
    lv_obj_set_pos(o, x, y);
  }
  tos_ui_obj_t handle = track(s, o, UI_T_LABEL);
  if (handle == TOS_UI_NONE && o != NULL)
    lv_obj_del(o); // table full: don't leak the widget
  ui_release();
  return handle;
}

static tos_ui_obj_t u_rect(tos_ui_obj_t parent, int32_t x, int32_t y, int32_t w, int32_t h) {
  if (!tos_app_cap_check(TOS_CAP_UI))
    return TOS_UI_NONE;
  ui_session_t *s = session_self();
  if (s == NULL || s->screen == NULL || s->canvas || !ui_acquire())
    return TOS_UI_NONE;
  lv_obj_t *o = lv_obj_create(parent_obj(s, parent));
  if (o != NULL) {
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  }
  tos_ui_obj_t handle = track(s, o, UI_T_RECT);
  if (handle == TOS_UI_NONE && o != NULL)
    lv_obj_del(o);
  ui_release();
  return handle;
}

static tos_ui_obj_t
u_bar(tos_ui_obj_t parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t min, int32_t max) {
  if (!tos_app_cap_check(TOS_CAP_UI))
    return TOS_UI_NONE;
  ui_session_t *s = session_self();
  if (s == NULL || s->screen == NULL || s->canvas || !ui_acquire())
    return TOS_UI_NONE;
  lv_obj_t *o = lv_bar_create(parent_obj(s, parent));
  if (o != NULL) {
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_bar_set_range(o, min, max);
    lv_bar_set_value(o, min, LV_ANIM_OFF);
  }
  tos_ui_obj_t handle = track(s, o, UI_T_BAR);
  if (handle == TOS_UI_NONE && o != NULL)
    lv_obj_del(o);
  ui_release();
  return handle;
}

// Decode an app-supplied LVGL .bin ([magic_cf u32][w u16][h u16][pixels]) into a
// firmware-owned image dsc + pixel copy, bounded to the panel. NULL on anything
// malformed or oversized. Same wire format as the .hb icon / asset pipeline.
static lv_image_dsc_t *decode_image_bin(const void *bin, uint32_t len) {
  const uint8_t *b = (const uint8_t *)bin;
  if (b == NULL || len < UI_IMG_HDR)
    return NULL;
  uint32_t magic_cf = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
                      ((uint32_t)b[3] << 24);
  uint16_t w = (uint16_t)(b[4] | (b[5] << 8));
  uint16_t h = (uint16_t)(b[6] | (b[7] << 8));
  if (w == 0 || h == 0 || w > UI_SCREEN_W || h > UI_SCREEN_H)
    return NULL;
  lv_color_format_t cf = LV_COLOR_FORMAT_ARGB8888;
  if ((magic_cf & 0xFF) == LV_IMAGE_HEADER_MAGIC)
    cf = (lv_color_format_t)((magic_cf >> 8) & 0xFF);
  uint32_t stride = lv_draw_buf_width_to_stride(w, cf);
  uint32_t data_size = stride * h;
  if (cf == LV_COLOR_FORMAT_RGB565A8)
    data_size += (stride / 2) * h;
  if ((uint32_t)(len - UI_IMG_HDR) < data_size)
    return NULL;
  lv_image_dsc_t *dsc = heap_caps_malloc(sizeof(*dsc), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (dsc == NULL)
    return NULL;
  uint8_t *px = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (px == NULL) {
    heap_caps_free(dsc);
    return NULL;
  }
  memcpy(px, b + UI_IMG_HDR, data_size);
  memset(dsc, 0, sizeof(*dsc));
  dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc->header.cf = cf;
  dsc->header.w = w;
  dsc->header.h = h;
  dsc->header.stride = stride;
  dsc->data = px;
  dsc->data_size = data_size;
  return dsc;
}

static void free_image_dsc(lv_image_dsc_t *dsc) {
  if (dsc == NULL)
    return;
  if (dsc->data != NULL)
    heap_caps_free((void *)dsc->data);
  heap_caps_free(dsc);
}

// Frees the dsc + pixels when LVGL deletes the image (after its last render), so
// the copy safely outlives the app's bytes and any async screen teardown.
static void image_free_cb(lv_event_t *e) {
  free_image_dsc((lv_image_dsc_t *)lv_event_get_user_data(e));
}

static tos_ui_obj_t
u_image(tos_ui_obj_t parent, int32_t x, int32_t y, const void *bin, uint32_t bin_len) {
  if (!tos_app_cap_check(TOS_CAP_UI))
    return TOS_UI_NONE;
  ui_session_t *s = session_self();
  if (s == NULL || s->screen == NULL || s->canvas || !ui_acquire())
    return TOS_UI_NONE;
  lv_image_dsc_t *dsc = decode_image_bin(bin, bin_len);
  if (dsc == NULL) {
    ui_release();
    return TOS_UI_NONE;
  }
  lv_obj_t *o = lv_image_create(parent_obj(s, parent));
  if (o == NULL) {
    free_image_dsc(dsc);
    ui_release();
    return TOS_UI_NONE;
  }
  lv_image_set_src(o, dsc);
  lv_obj_set_pos(o, x, y);
  lv_obj_add_event_cb(o, image_free_cb, LV_EVENT_DELETE, dsc);
  tos_ui_obj_t handle = track(s, o, UI_T_IMAGE);
  if (handle == TOS_UI_NONE)
    lv_obj_del(o); // fires image_free_cb -> frees dsc + pixels
  ui_release();
  return handle;
}

// --- mutators -----------------------------------------------------------------

static esp_err_t u_set_text(tos_ui_obj_t h, const char *text) {
  NEED(TOS_CAP_UI);
  ui_session_t *s = session_self();
  if (s == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  lv_obj_t *o = resolve_t(s, h, UI_T_LABEL);
  esp_err_t e = ESP_ERR_NOT_FOUND;
  if (o != NULL) {
    lv_label_set_text(o, text ? text : ""); // copies
    e = ESP_OK;
  }
  ui_release();
  return e;
}

static esp_err_t u_set_value(tos_ui_obj_t h, int32_t v) {
  NEED(TOS_CAP_UI);
  ui_session_t *s = session_self();
  if (s == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  lv_obj_t *o = resolve_t(s, h, UI_T_BAR);
  esp_err_t e = ESP_ERR_NOT_FOUND;
  if (o != NULL) {
    lv_bar_set_value(o, v, LV_ANIM_OFF);
    e = ESP_OK;
  }
  ui_release();
  return e;
}

static esp_err_t u_set_pos(tos_ui_obj_t h, int32_t x, int32_t y) {
  NEED(TOS_CAP_UI);
  ui_session_t *s = session_self();
  if (s == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  lv_obj_t *o = resolve(s, h);
  esp_err_t e = ESP_ERR_NOT_FOUND;
  if (o != NULL) {
    lv_obj_set_pos(o, x, y);
    e = ESP_OK;
  }
  ui_release();
  return e;
}

static esp_err_t u_set_size(tos_ui_obj_t h, int32_t w, int32_t ht) {
  NEED(TOS_CAP_UI);
  ui_session_t *s = session_self();
  if (s == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  lv_obj_t *o = resolve(s, h);
  esp_err_t e = ESP_ERR_NOT_FOUND;
  if (o != NULL) {
    lv_obj_set_size(o, w, ht);
    e = ESP_OK;
  }
  ui_release();
  return e;
}

static esp_err_t u_set_text_color(tos_ui_obj_t h, tos_rgb_t c) {
  NEED(TOS_CAP_UI);
  ui_session_t *s = session_self();
  if (s == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  lv_obj_t *o = resolve(s, h);
  esp_err_t e = ESP_ERR_NOT_FOUND;
  if (o != NULL) {
    lv_obj_set_style_text_color(o, rgb(c), 0);
    e = ESP_OK;
  }
  ui_release();
  return e;
}

static esp_err_t u_set_bg_color(tos_ui_obj_t h, tos_rgb_t c) {
  NEED(TOS_CAP_UI);
  ui_session_t *s = session_self();
  if (s == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  lv_obj_t *o = resolve(s, h);
  esp_err_t e = ESP_ERR_NOT_FOUND;
  if (o != NULL) {
    lv_obj_set_style_bg_color(o, rgb(c), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    e = ESP_OK;
  }
  ui_release();
  return e;
}

static esp_err_t u_set_hidden(tos_ui_obj_t h, bool hidden) {
  NEED(TOS_CAP_UI);
  ui_session_t *s = session_self();
  if (s == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  lv_obj_t *o = resolve(s, h);
  esp_err_t e = ESP_ERR_NOT_FOUND;
  if (o != NULL) {
    if (hidden)
      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    e = ESP_OK;
  }
  ui_release();
  return e;
}

static esp_err_t u_del(tos_ui_obj_t h) {
  NEED(TOS_CAP_UI);
  ui_session_t *s = session_self();
  if (s == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  int idx = (int)(h & 0xFFFF) - 1;
  uint16_t gen = (uint16_t)(h >> 16);
  esp_err_t e = ESP_ERR_NOT_FOUND;
  if (idx >= 0 && idx < UI_MAX_OBJS && s->w[idx].used && s->w[idx].gen == gen &&
      s->w[idx].obj != NULL && lv_obj_is_valid(s->w[idx].obj)) {
    lv_obj_del(s->w[idx].obj); // also frees descendants; their handles go stale and
    s->w[idx].used = false;    // resolve() rejects them via lv_obj_is_valid
    s->w[idx].gen++;
    s->w[idx].obj = NULL;
    e = ESP_OK;
  }
  ui_release();
  return e;
}

static esp_err_t u_batch(tos_ui_batch_cb cb, void *user) {
  NEED(TOS_CAP_UI);
  if (cb == NULL)
    return ESP_ERR_INVALID_ARG;
  ui_session_t *s = session_self();
  if (s == NULL || s->screen == NULL || !ui_acquire())
    return ESP_ERR_INVALID_STATE;
  cb(user); // app issues builders/setters; their per-call locks nest (recursive)
  ui_release();
  return ESP_OK;
}

static int u_poll_event(tos_ui_event_t *out) {
  if (out == NULL || !tos_app_cap_check(TOS_CAP_UI))
    return 0;
  ui_session_t *s = session_self();
  if (s == NULL || s->evq == NULL)
    return 0;
  return xQueueReceive(s->evq, out, 0) == pdTRUE ? 1 : 0;
}

const tos_ui_api_t tos_ui_api_impl = {
    .screen_open = u_screen_open,
    .screen_close = u_screen_close,
    .metrics = u_metrics,
    .label = u_label,
    .rect = u_rect,
    .bar = u_bar,
    .set_text = u_set_text,
    .set_value = u_set_value,
    .set_pos = u_set_pos,
    .set_size = u_set_size,
    .set_text_color = u_set_text_color,
    .set_bg_color = u_set_bg_color,
    .set_hidden = u_set_hidden,
    .del = u_del,
    .batch = u_batch,
    .poll_event = u_poll_event,
    .canvas_begin = u_canvas_begin,
    .canvas_blit = u_canvas_blit,
    .canvas_end = u_canvas_end,
    .image = u_image,
};
