// HighBoy (ESP32-P4) platform layer for doomgeneric.
//
// Implements the six DG_* hooks + a launcher that runs DOOM in its own task:
//  - DG_DrawFrame : DG_ScreenBuffer (ARGB8888, DOOMGENERIC_RESX x RESY) -> RGB565
//                   big-endian, blitted directly to the ST7789 (bypassing LVGL)
//                   in landscape (320x240), letterboxed to DOOM's 320x200.
//  - DG_GetKey    : drained from a ring buffer filled by a polling input task that
//                   maps the 6 physical buttons (D-pad + OK + BACK) to DOOM keys,
//                   with an OK+BACK chord = ESC (menu) and hold-both ~2s = quit.
//  - timing       : esp_timer / FreeRTOS ticks.
// The WAD is streamed from /sdcard/doom1.wad. Zone memory comes from PSRAM
// automatically (CONFIG_SPIRAM_USE_MALLOC: large mallocs land in PSRAM).
//
// Exit model: doomgeneric_Create() runs DOOM's own infinite loop and never
// returns, so "quit to launcher" = esp_restart() (clean reboot into the UI).

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "doom_highboy.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h" // xTaskCreatePinnedToCoreWithCaps
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"

#include "st7789.h"       // extern panel_handle
#include "buttons_gpio.h" // *_button_is_down()
#include "lvgl_glue.h"    // lvgl_glue_direct_begin()
#include "storage_init.h" // storage_is_mounted(), storage_init()

static const char *TAG = "DOOM";

// ---- geometry -------------------------------------------------------------
#define DG_W    DOOMGENERIC_RESX // 320
#define DG_H    DOOMGENERIC_RESY // 200
#define PANEL_W 320              // ST7789 in landscape (swap_xy)
#define PANEL_H 240
#define Y_OFF   ((PANEL_H - DG_H) / 2) // 20px letterbox top/bottom
#define STRIP   20                     // blit height per SPI burst (avoid corruption)

static uint16_t *s_fb[2]; // double-buffered RGB565 (big-endian) frames
static int s_fb_sel;

// Pulsed once per frame to keep the sys_monitor render-liveness beat alive while
// DOOM owns the panel and the LVGL task is parked (see doom_main_task).
static void (*s_beat_kick)(void);

// ---- timing ---------------------------------------------------------------
uint32_t DG_GetTicksMs(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}
void DG_SleepMs(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}
void DG_SetWindowTitle(const char *title) {
  (void)title;
}

// ---- input ring buffer (filled by doom_input_task, drained by DG_GetKey) --
#define KQ 64
static volatile struct {
  int pressed;
  unsigned char key;
} s_kq[KQ];
static volatile int s_kq_head, s_kq_tail;

static void kq_push(int pressed, unsigned char key) {
  int n = (s_kq_head + 1) % KQ;
  if (n == s_kq_tail)
    return; // full: drop
  s_kq[s_kq_head].pressed = pressed;
  s_kq[s_kq_head].key = key;
  s_kq_head = n;
}

int DG_GetKey(int *pressed, unsigned char *key) {
  if (s_kq_tail == s_kq_head)
    return 0;
  *pressed = s_kq[s_kq_tail].pressed;
  *key = s_kq[s_kq_tail].key;
  s_kq_tail = (s_kq_tail + 1) % KQ;
  return 1;
}

// Simple edge helper for the D-pad (one-shot press/release into the ring).
static void edge(bool now, bool *prev, unsigned char key) {
  if (now && !*prev) {
    kq_push(1, key);
    *prev = true;
  } else if (!now && *prev) {
    kq_push(0, key);
    *prev = false;
  }
}

// Poll the 6 buttons and translate to DOOM keys. Mapping:
//   D-pad  -> arrows, ROTATED 90° for landscape (UP->LEFT DOWN->RIGHT RIGHT->UP LEFT->DOWN)
//   OK     -> FIRE (+ENTER so it also selects in menus)
//   BACK   -> USE (open doors/switches)
//   OK+BACK pressed together (within ~250ms) -> ESCAPE (open/close menu)
//   OK+BACK held together ~2s               -> quit to launcher (esp_restart)
static void doom_input_task(void *arg) {
  (void)arg;
  bool p_up = 0, p_dn = 0, p_l = 0, p_r = 0;
  bool ok_c = 0, bk_c = 0;   // OK / BACK committed as their own key
  bool ok_sw = 0, bk_sw = 0; // swallow until physical release (post-chord)
  bool chord = 0;            // ESC chord active
  int64_t t_ok = 0, t_bk = 0, t_chord = 0;

  for (;;) {
    bool up = up_button_is_down(), dn = down_button_is_down();
    bool l = left_button_is_down(), r = right_button_is_down();
    bool ok = ok_button_is_down(), bk = back_button_is_down();
    int64_t now = esp_timer_get_time() / 1000;

    // D-pad rotated 90° for landscape play (device held sideways):
    edge(up, &p_up, KEY_LEFTARROW);  // physical UP    -> game LEFT
    edge(dn, &p_dn, KEY_RIGHTARROW); // physical DOWN  -> game RIGHT
    edge(r, &p_r, KEY_UPARROW);      // physical RIGHT -> game UP
    edge(l, &p_l, KEY_DOWNARROW);    // physical LEFT  -> game DOWN

    // Track when OK / BACK physically went down (for the chord window).
    static bool pok = 0, pbk = 0;
    if (ok && !pok)
      t_ok = now;
    if (bk && !pbk)
      t_bk = now;

    if (chord) {
      if (ok && bk) {
        if (now - t_chord >= 2000) { // hold both ~2s -> quit to launcher
          ESP_LOGW(TAG, "quit gesture -> esp_restart()");
          vTaskDelay(pdMS_TO_TICKS(50));
          esp_restart();
        }
      } else { // one released -> close chord
        kq_push(0, KEY_ESCAPE);
        chord = 0;
        ok_sw = ok; // swallow whatever is still held
        bk_sw = bk;
      }
    } else if (ok && bk && !ok_sw && !bk_sw && (t_ok > t_bk ? t_ok - t_bk : t_bk - t_ok) <= 250) {
      // enter chord: retract any single-key commits, send ESC down
      if (ok_c) {
        kq_push(0, KEY_FIRE);
        kq_push(0, KEY_ENTER);
        ok_c = 0;
      }
      if (bk_c) {
        kq_push(0, KEY_USE);
        bk_c = 0;
      }
      kq_push(1, KEY_ESCAPE);
      chord = 1;
      t_chord = now;
    } else {
      // normal handling; honor swallow-until-release
      if (ok_sw && !ok)
        ok_sw = 0;
      if (bk_sw && !bk)
        bk_sw = 0;
      bool ok_eff = ok && !ok_sw;
      bool bk_eff = bk && !bk_sw;
      if (ok_eff && !ok_c) {
        kq_push(1, KEY_FIRE);
        kq_push(1, KEY_ENTER);
        ok_c = 1;
      } else if (!ok_eff && ok_c) {
        kq_push(0, KEY_FIRE);
        kq_push(0, KEY_ENTER);
        ok_c = 0;
      }
      if (bk_eff && !bk_c) {
        kq_push(1, KEY_USE);
        bk_c = 1;
      } else if (!bk_eff && bk_c) {
        kq_push(0, KEY_USE);
        bk_c = 0;
      }
    }

    pok = ok;
    pbk = bk;
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

// ---- framebuffer ----------------------------------------------------------
static inline uint16_t argb_to_565be(uint32_t p) {
  uint8_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
  uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return (uint16_t)((c >> 8) | (c << 8)); // ST7789 wants big-endian RGB565
}

static void panel_clear_black(void) {
  // Fill the whole 320x240 landscape panel black once (letterbox stays black).
  uint16_t *row =
      heap_caps_malloc(PANEL_W * STRIP * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!row)
    return;
  memset(row, 0, PANEL_W * STRIP * sizeof(uint16_t));
  for (int y = 0; y < PANEL_H; y += STRIP) {
    int rows = (y + STRIP <= PANEL_H) ? STRIP : (PANEL_H - y);
    esp_lcd_panel_draw_bitmap(panel_handle, 0, y, PANEL_W, y + rows, row);
  }
  vTaskDelay(pdMS_TO_TICKS(30)); // let the blits drain before freeing
  heap_caps_free(row);
}

void DG_Init(void) {
  for (int i = 0; i < 2; i++)
    s_fb[i] = heap_caps_malloc(DG_W * DG_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  ESP_LOGW(TAG,
           "DG_Init: fb0=%p fb1=%p (%d bytes each)",
           s_fb[0],
           s_fb[1],
           DG_W * DG_H * (int)sizeof(uint16_t));
  // Input runs in its own task (DOOM's loop blocks this task via Create()).
  // Stack in PSRAM — internal RAM is scarce (~30 KB free after full boot).
  xTaskCreatePinnedToCoreWithCaps(
      doom_input_task, "doom_in", 4096, NULL, 6, NULL, 0, MALLOC_CAP_SPIRAM);
}

void DG_DrawFrame(void) {
  uint16_t *fb = s_fb[s_fb_sel];
  if (!fb)
    return;
  s_fb_sel ^= 1; // ping-pong so the previous frame's DMA can still be in flight

  const uint32_t *src = (const uint32_t *)DG_ScreenBuffer;
  const int n = DG_W * DG_H;
  for (int i = 0; i < n; i++)
    fb[i] = argb_to_565be(src[i]);

  for (int y = 0; y < DG_H; y += STRIP) {
    int rows = (y + STRIP <= DG_H) ? STRIP : (DG_H - y);
    esp_lcd_panel_draw_bitmap(panel_handle, 0, Y_OFF + y, DG_W, Y_OFF + y + rows, &fb[y * DG_W]);
  }
}

// ---- WAD discovery --------------------------------------------------------
static bool ends_with_wad(const char *name) {
  size_t n = strlen(name);
  return n >= 4 && strcasecmp(name + n - 4, ".wad") == 0;
}

// List a directory to the log and return the first *.wad found (full path).
static bool scan_dir_for_wad(const char *dir, char *out, size_t outsz) {
  DIR *d = opendir(dir);
  if (!d)
    return false;
  ESP_LOGW(TAG, "listing %s:", dir);
  bool found = false;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    ESP_LOGI(TAG, "   %s", e->d_name);
    if (!found && ends_with_wad(e->d_name)) {
      snprintf(out, outsz, "%s/%s", dir, e->d_name);
      found = true;
    }
  }
  closedir(d);
  return found;
}

// Find a WAD: try common exact paths, then scan the SD root and /sdcard/doom
// for any *.wad. Robust to subfolders and name/case differences.
static bool find_wad(char *out, size_t outsz) {
  static const char *cands[] = {"/sdcard/doom1.wad",
                                "/sdcard/doom.wad",
                                "/sdcard/DOOM1.WAD",
                                "/sdcard/DOOM.WAD",
                                "/sdcard/doom/doom1.wad",
                                "/sdcard/doom/doom.wad",
                                NULL};
  for (int i = 0; cands[i]; i++) {
    FILE *f = fopen(cands[i], "rb");
    if (f) {
      fclose(f);
      snprintf(out, outsz, "%s", cands[i]);
      return true;
    }
  }
  if (scan_dir_for_wad("/sdcard", out, outsz))
    return true;
  if (scan_dir_for_wad("/sdcard/doom", out, outsz))
    return true;
  return false;
}

// ---- launcher -------------------------------------------------------------
static void doom_main_task(void *arg) {
  (void)arg;
  ESP_LOGW(TAG,
           "doom_main_task: enter (free int=%u psram=%u)",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  vTaskDelay(pdMS_TO_TICKS(150)); // let the UI screen switch settle

  // Own the display exclusively: direct-draw routes the panel's DMA-done callback
  // to DOOM (not LVGL), and holding the LVGL lock forever parks the LVGL task so
  // it never flushes or fights DOOM for the SPI3 bus. The sys_monitor render beat
  // is fed by s_beat_kick() in the loop instead of by LVGL. Quitting reboots, so
  // the lock is never released.
  lvgl_glue_direct_begin();
  lvgl_glue_lock(-1);
  ESP_LOGW(TAG, "doom_main_task: panel owned (LVGL parked)");

  // Landscape 320x240 so DOOM's 320-wide frame fits (portrait is only 240 wide).
  esp_lcd_panel_swap_xy(panel_handle, true);
  esp_lcd_panel_mirror(panel_handle, true, false);
  panel_clear_black();
  ESP_LOGW(TAG, "doom_main_task: panel landscape + cleared");

  if (!storage_is_mounted()) {
    ESP_LOGW(TAG, "SD not mounted, mounting...");
    storage_init();
  }
  // Locate a WAD (lists the SD so you can see what's there). Fail loudly rather
  // than hang inside DOOM's I_Error if none is present.
  static char wadpath[128];
  if (!find_wad(wadpath, sizeof(wadpath))) {
    ESP_LOGE(TAG, "No .wad found on SD (checked /sdcard and /sdcard/doom).");
    ESP_LOGE(TAG, "Copy doom1.wad to the SD root, then reboot.");
    for (;;)
      vTaskDelay(pdMS_TO_TICKS(2000));
  }
  chdir("/sdcard"); // DOOM writes config/saves relative to CWD (SD mount point)

  ESP_LOGW(TAG, "starting DOOM (wad=%s). Quit = hold OK+BACK ~2s.", wadpath);
  static char *argv[] = {"doom", "-iwad", wadpath, NULL};
  doomgeneric_Create(3, argv); // init only (D_DoomMain/D_DoomLoop return after 1 tick)

  // doomgeneric's design: the platform drives the game loop by calling
  // doomgeneric_Tick() repeatedly (D_DoomLoop runs ONE tick then returns).
  for (;;) {
    doomgeneric_Tick();
    // LVGL is parked, so feed the render-liveness beat ourselves.
    if (s_beat_kick != NULL)
      s_beat_kick();
    // Yield 1 tick (1ms @1kHz) so core 1's IDLE task runs and resets the task
    // watchdog; doomgeneric's tic loop never blocks on its own.
    vTaskDelay(1);
  }
}

void highboy_doom_start(void (*render_beat_kick)(void)) {
  s_beat_kick = render_beat_kick;
  // 48 KB stack in PSRAM: DOOM recurses deeply (R_RenderBSPNode) and internal
  // RAM is nearly exhausted after boot (~30 KB), so an internal stack can't be
  // allocated. WithCaps puts the stack in the ample 32 MB PSRAM.
  BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
      doom_main_task, "doom", 49152, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM);
  ESP_LOGW(TAG, "highboy_doom_start: task create -> %s", ok == pdPASS ? "OK" : "FAILED");
}
