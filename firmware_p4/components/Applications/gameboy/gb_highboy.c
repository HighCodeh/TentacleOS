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

#include "gb_highboy.h"
#include "gb_rom_validation.h"
#include "gb_input_mapping.h"
#include "gb_save_io.h"

#include <dirent.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "audio_i2s.h"
#include "buttons_gpio.h"
#include "input_manager.h"
#include "lvgl_glue.h"
#include "st7789.h"
#include "storage_init.h"
#include "sys_prio.h"

void ui_render_beat_kick(void);

#define GB_OPEN_BUS 0xFF

#define ENABLE_LCD 1
#include "minigb_apu.h"
static struct minigb_apu_ctx *s_apu = NULL;
uint8_t audio_read(const uint16_t addr) {
  return s_apu ? minigb_apu_audio_read(s_apu, addr) : GB_OPEN_BUS;
}
void audio_write(const uint16_t addr, const uint8_t val) {
  if (s_apu)
    minigb_apu_audio_write(s_apu, addr, val);
}
#include "peanut_gb.h"

static const char *TAG = "GB";

#define GB_JOYPAD_IDLE 0xFF

#define GB_DST_W      320
#define GB_DST_H      240
#define GB_X_OFF      ((320 - GB_DST_W) / 2)
#define GB_Y_OFF      ((240 - GB_DST_H) / 2)
#define GB_STRIP_ROWS 24

#define GB_ROM_PATH_LEN 300
#define GB_MAIN_STACK   32768
#define GB_AUDIO_STACK  4096

#define GB_STARTUP_DELAY_MS 150
#define GB_AUTOSAVE_MS      3000
#define GB_FLUSH_WAIT_MS    100
#define GB_DMA_DRAIN_MS     30
#define GB_AUDIO_POLL_MS    10
#define GB_AUDIO_STOP_TRIES 100

#define GB_FRAME_US  (1000000 / 60)
#define GB_RESYNC_US 250000

static const uint32_t GB_DMG_PALETTE[] = {0xE0F8D0, 0x88C070, 0x346856, 0x081820};
#define GB_DMG_PALETTE_COUNT (sizeof(GB_DMG_PALETTE) / sizeof(GB_DMG_PALETTE[0]))

static struct gb_s *s_gb;
static uint8_t *s_rom;
static size_t s_rom_size;
static uint8_t *s_cram;
static size_t s_cram_size;
static uint8_t *s_shade;
static uint16_t *s_strip;
static int s_strip_rows; // strip height actually allocated (adapts to free DMA RAM)
static uint16_t s_pal[GB_DMG_PALETTE_COUNT];
static uint8_t s_sx[GB_DST_W], s_sy[GB_DST_H];

static char s_rompath[GB_ROM_PATH_LEN];
static char s_savepath[GB_ROM_PATH_LEN + 5];
static bool s_has_save = false;
static volatile bool s_cram_dirty = false;
static int64_t s_cram_dirty_ms = 0;

static volatile bool s_exit_req = false;
static volatile bool s_finished = false;
static volatile bool s_running = false;
static volatile bool s_audio_run = true;
static volatile bool s_audio_done = true;
static char s_last_error[96];
static jmp_buf s_core_error_jmp;
static bool s_core_error_armed = false;
static highboy_gb_input_state_t s_input_state;

_Static_assert(JOYPAD_A == 0x01 && JOYPAD_B == 0x02 && JOYPAD_SELECT == 0x04 &&
                   JOYPAD_START == 0x08 && JOYPAD_RIGHT == 0x10 && JOYPAD_LEFT == 0x20 &&
                   JOYPAD_UP == 0x40 && JOYPAD_DOWN == 0x80,
               "DMG input bit layout changed");

bool highboy_gb_finished(void) {
  return s_finished;
}

const char *highboy_gb_last_error(void) {
  return s_last_error;
}

static void set_error(const char *message) {
  strlcpy(s_last_error, message, sizeof(s_last_error));
  ESP_LOGE(TAG, "%s", s_last_error);
}

/* Leaves a successful file positioned at its beginning for the later read. */
static const char *check_rom_file(FILE *file, size_t *rom_bytes) {
  uint8_t header[HIGHBOY_GB_HEADER_BYTES];
  if (fseek(file, 0, SEEK_END) != 0)
    return "Unable to seek ROM file";
  long size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0)
    return "Unable to read ROM file size";
  size_t read_bytes = fread(header, 1, sizeof(header), file);
  if (ferror(file))
    return "SD error while reading ROM header";
  const char *error = highboy_gb_header_error(header, read_bytes, (size_t)size);
  if (error)
    return error;
  if (fseek(file, 0, SEEK_SET) != 0)
    return "Unable to seek ROM file";
  if (rom_bytes)
    *rom_bytes = (size_t)size;
  return NULL;
}

bool highboy_gb_validate_rom(const char *rompath, char *reason, size_t reason_size) {
  const char *error = NULL;
  FILE *file = NULL;
  if (!rompath || !rompath[0] || strlen(rompath) >= GB_ROM_PATH_LEN)
    error = "Invalid or excessively long ROM path";
  else if (!(file = fopen(rompath, "rb")))
    error = "Unable to open ROM on microSD";
  else
    error = check_rom_file(file, NULL);
  if (file)
    fclose(file);
  if (reason && reason_size)
    strlcpy(reason, error ? error : "", reason_size);
  return error == NULL;
}

static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
  (void)gb;
  return addr < s_rom_size ? s_rom[addr] : GB_OPEN_BUS;
}

static uint8_t cram_read(struct gb_s *gb, const uint_fast32_t addr) {
  (void)gb;
  return (s_cram && addr < s_cram_size) ? s_cram[addr] : GB_OPEN_BUS;
}

static void cram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t v) {
  (void)gb;
  if (s_cram && addr < s_cram_size) {
    s_cram[addr] = v;
    s_cram_dirty = true;
    s_cram_dirty_ms = esp_timer_get_time() / 1000;
  }
}

static void gb_err(struct gb_s *gb, const enum gb_error_e e, const uint16_t addr) {
  (void)gb;
  ESP_LOGE(TAG, "gb_error %d @ 0x%04X", (int)e, addr);
  snprintf(s_last_error, sizeof(s_last_error), "Emulator error %d at %04X", (int)e, addr);
  /* Peanut-GB declares invalid opcodes unreachable; returning here is unsafe. */
  if (s_core_error_armed)
    longjmp(s_core_error_jmp, 1);
}

static void lcd_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
  (void)gb;
  if (line >= LCD_HEIGHT)
    return;
  uint8_t *dst = s_shade + (size_t)line * LCD_WIDTH;
  for (int x = 0; x < LCD_WIDTH; x++)
    dst[x] = pixels[x] & LCD_COLOUR;
}

static inline uint16_t to565be(uint32_t rgb) {
  uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return (uint16_t)((c >> 8) | (c << 8));
}

static bool find_gb_rom(char *out, size_t outsz) {
  const char *dirs[] = {"/sdcard/games/gameboy", "/sdcard", "/sdcard/gb", "/sdcard/roms", NULL};
  for (int d = 0; dirs[d]; d++) {
    DIR *dir = opendir(dirs[d]);
    if (!dir)
      continue;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
      size_t n = strlen(e->d_name);
      bool gb = (n >= 3 && strcasecmp(e->d_name + n - 3, ".gb") == 0);
      bool gbc = (n >= 4 && strcasecmp(e->d_name + n - 4, ".gbc") == 0);
      if (gb || gbc) {
        snprintf(out, outsz, "%s/%s", dirs[d], e->d_name);
        closedir(dir);
        return true;
      }
    }
    closedir(dir);
  }
  return false;
}

static void derive_savepath(void) {
  strncpy(s_savepath, s_rompath, sizeof(s_savepath) - 1);
  s_savepath[sizeof(s_savepath) - 1] = '\0';
  char *dot = strrchr(s_savepath, '.');
  char *slash = strrchr(s_savepath, '/');
  if (dot && (!slash || dot > slash))
    *dot = '\0';
  strncat(s_savepath, ".sav", sizeof(s_savepath) - strlen(s_savepath) - 1);
}

static bool load_cram(void) {
  if (!s_has_save || !s_cram)
    return true;
  bool present = false;
  const char *error = gb_save_load_checked(s_savepath, s_cram, s_cram_size, &present);
  if (error) {
    set_error(error);
    return false;
  }
  ESP_LOGW(
      TAG, "%s (%u B) %s", present ? "save loaded" : "new save", (unsigned)s_cram_size, s_savepath);
  return true;
}

static bool save_cram(void) {
  if (!s_has_save || !s_cram || s_cram_size <= 1)
    return true;
  const char *error = gb_save_commit_checked(s_savepath, s_cram, s_cram_size);
  if (error) {
    set_error(error);
    return false;
  }
  s_cram_dirty = false;
  ESP_LOGW(TAG, "save verified (%u B) -> %s", (unsigned)s_cram_size, s_savepath);
  return true;
}

static void drain_menu_input(void) {
  /* LVGL is parked during play. Do not leave its menu queue full of game input. */
  input_event_t event;
  while (input_get_event(&event, 0)) {}
  for (input_button_t b = 0; b < INPUT_BTN_COUNT; ++b)
    (void)input_consume_press(b);
}

static void poll_input(void) {
  uint8_t physical = 0;
  if (up_button_is_down())
    physical |= GB_PHYS_UP;
  if (down_button_is_down())
    physical |= GB_PHYS_DOWN;
  if (left_button_is_down())
    physical |= GB_PHYS_LEFT;
  if (right_button_is_down())
    physical |= GB_PHYS_RIGHT;
  if (ok_button_is_down())
    physical |= GB_PHYS_OK;
  if (back_button_is_down())
    physical |= GB_PHYS_BACK;
  bool exit_requested = false;
  s_gb->direct.joypad = highboy_gb_input_update(
      &s_input_state, physical, (uint32_t)(esp_timer_get_time() / 1000), &exit_requested);
  if (exit_requested)
    s_exit_req = true;
  drain_menu_input();
}

static void blit_frame(void) {
  for (int y0 = 0; y0 < GB_DST_H; y0 += s_strip_rows) {
    int rows = (y0 + s_strip_rows <= GB_DST_H) ? s_strip_rows : (GB_DST_H - y0);
    for (int j = 0; j < rows; j++) {
      const uint8_t *srow = s_shade + (size_t)s_sy[y0 + j] * LCD_WIDTH;
      uint16_t *orow = s_strip + (size_t)j * GB_DST_W;
      for (int x = 0; x < GB_DST_W; x++)
        orow[x] = s_pal[srow[s_sx[x]]];
    }
    esp_lcd_panel_draw_bitmap(
        panel_handle, GB_X_OFF, GB_Y_OFF + y0, GB_X_OFF + GB_DST_W, GB_Y_OFF + y0 + rows, s_strip);
    lvgl_glue_wait_flush(GB_FLUSH_WAIT_MS);
  }
}

static void panel_clear_black(void) {
  memset(s_strip, 0, (size_t)GB_DST_W * s_strip_rows * sizeof(uint16_t));
  for (int y = 0; y < GB_DST_H; y += s_strip_rows) {
    int rows = (y + s_strip_rows <= GB_DST_H) ? s_strip_rows : (GB_DST_H - y);
    esp_lcd_panel_draw_bitmap(panel_handle, 0, y, GB_DST_W, y + rows, s_strip);
    lvgl_glue_wait_flush(GB_FLUSH_WAIT_MS);
  }
  vTaskDelay(pdMS_TO_TICKS(GB_DMA_DRAIN_MS));
}

static void gb_audio_task(void *arg) {
  (void)arg;
  if (audio_i2s_stream_start(AUDIO_SAMPLE_RATE) != ESP_OK) {
    ESP_LOGE(TAG, "audio stream start failed - no sound");
    s_audio_done = true;
    vTaskDeleteWithCaps(NULL);
    return;
  }
  int16_t *st = heap_caps_malloc(AUDIO_SAMPLES_TOTAL * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  int16_t *mo = heap_caps_malloc(AUDIO_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (st == NULL || mo == NULL) {
    ESP_LOGE(TAG, "audio buf alloc failed");
    free(st);
    free(mo);
    audio_i2s_stream_stop();
    s_audio_done = true;
    vTaskDeleteWithCaps(NULL);
    return;
  }
  ESP_LOGW(TAG, "audio task running @ %d Hz", AUDIO_SAMPLE_RATE);
  while (s_audio_run) {
    if (s_apu == NULL) {
      vTaskDelay(pdMS_TO_TICKS(GB_AUDIO_POLL_MS));
      continue;
    }
    minigb_apu_audio_callback(s_apu, st);
    for (unsigned i = 0; i < AUDIO_SAMPLES; i++)
      mo[i] = (int16_t)(((int)st[2 * i] + (int)st[2 * i + 1]) >> 1);
    audio_i2s_stream_write(mo, AUDIO_SAMPLES);
  }
  free(st);
  free(mo);
  s_audio_done = true;
  vTaskDeleteWithCaps(NULL);
}

static void gb_main_task(void *arg) {
  (void)arg;
  FILE *f = NULL;
  bool direct_started = false;
  bool save_ready = false;
  ESP_LOGW(TAG,
           "gb_main_task: enter (free int=%u psram=%u)",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  vTaskDelay(pdMS_TO_TICKS(GB_STARTUP_DELAY_MS));

  if (!storage_is_mounted())
    storage_init();
  if (!storage_is_mounted()) {
    set_error("microSD is not mounted");
    goto teardown;
  }

  if (s_rompath[0] == '\0' && !find_gb_rom(s_rompath, sizeof(s_rompath))) {
    set_error("No Game Boy ROM found on microSD");
    goto teardown;
  }
  ESP_LOGW(TAG, "loading ROM: %s", s_rompath);
  f = fopen(s_rompath, "rb");
  if (f == NULL) {
    set_error("Unable to open ROM on microSD");
    goto teardown;
  }
  const char *validation_error = check_rom_file(f, &s_rom_size);
  if (validation_error) {
    set_error(validation_error);
    goto teardown;
  }
  s_rom = heap_caps_malloc(s_rom_size, MALLOC_CAP_SPIRAM);
  if (s_rom == NULL) {
    set_error("Not enough PSRAM for this ROM");
    goto teardown;
  }
  size_t rd = fread(s_rom, 1, s_rom_size, f);
  bool rom_read_failed = ferror(f) || rd != s_rom_size;
  fclose(f);
  f = NULL;
  if (rom_read_failed) {
    set_error("ROM read was incomplete; check microSD");
    goto teardown;
  }
  /* Validate the bytes actually loaded too, in case the card changed mid-read. */
  validation_error = highboy_gb_header_error(s_rom, s_rom_size, s_rom_size);
  if (validation_error) {
    set_error(validation_error);
    goto teardown;
  }
  ESP_LOGW(TAG, "ROM %u bytes read (%u)", (unsigned)rd, (unsigned)s_rom_size);

  s_gb = heap_caps_malloc(sizeof(struct gb_s), MALLOC_CAP_SPIRAM);
  s_shade = heap_caps_malloc((size_t)LCD_WIDTH * LCD_HEIGHT, MALLOC_CAP_SPIRAM);
  // Internal DMA RAM is scarce/fragmented (LVGL's draw buffers hold a large chunk),
  // so the full-height strip may not fit. Take the tallest strip that does, sizing
  // the first try to the largest free block (no noisy "alloc failed" log) and
  // halving down to a single row as a floor.
  {
    int rows = GB_STRIP_ROWS;
    size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    while (rows > 1 && (size_t)GB_DST_W * rows * sizeof(uint16_t) > avail)
      rows >>= 1;
    for (; rows >= 1 && s_strip == NULL; rows >>= 1) {
      s_strip = heap_caps_malloc((size_t)GB_DST_W * rows * sizeof(uint16_t),
                                 MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
      if (s_strip != NULL)
        s_strip_rows = rows;
    }
  }
  if (s_gb == NULL || s_shade == NULL || s_strip == NULL) {
    ESP_LOGE(TAG,
             "buffer alloc failed (int free=%u) - returning to launcher",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    set_error("Not enough memory for Game Boy video");
    goto teardown;
  }
  memset(s_shade, 0, (size_t)LCD_WIDTH * LCD_HEIGHT);

  enum gb_init_error_e ie = gb_init(s_gb, rom_read, cram_read, cram_write, gb_err, NULL);
  ESP_LOGW(TAG, "gb_init -> %d", (int)ie);
  if (ie != GB_INIT_NO_ERROR) {
    ESP_LOGE(TAG, "gb_init failed (%d) - unsupported cart?", (int)ie);
    set_error("Emulator rejected this cartridge");
    goto teardown;
  }

  size_t ram = 0;
  if (gb_get_save_size_s(s_gb, &ram) != 0) {
    set_error("Cartridge save size is unsupported");
    goto teardown;
  }
  s_has_save = (ram > 0);
  s_cram_size = ram ? ram : 1;
  s_cram = heap_caps_malloc(s_cram_size, MALLOC_CAP_SPIRAM);
  if (!s_cram) {
    set_error("Not enough PSRAM for cartridge save RAM");
    goto teardown;
  }
  memset(s_cram, 0, s_cram_size);
  gb_init_lcd(s_gb, lcd_line);

  derive_savepath();
  if (!load_cram())
    goto teardown;
  s_cram_dirty = false;
  save_ready = true;

  s_apu = heap_caps_malloc(sizeof(struct minigb_apu_ctx), MALLOC_CAP_SPIRAM);
  if (s_apu) {
    minigb_apu_audio_init(s_apu);
    s_audio_run = true;
    s_audio_done = false;
    BaseType_t audio_ok = xTaskCreatePinnedToCoreWithCaps(gb_audio_task,
                                                          "gb_audio",
                                                          GB_AUDIO_STACK,
                                                          NULL,
                                                          SYS_PRIO_SERVICE_HI,
                                                          NULL,
                                                          SYS_CORE_RADIO,
                                                          MALLOC_CAP_SPIRAM);
    if (audio_ok != pdPASS) {
      ESP_LOGW(TAG, "audio task creation failed - running without sound");
      s_audio_run = false;
      s_audio_done = true;
    }
  } else {
    ESP_LOGW(TAG, "APU alloc failed - running without sound");
    s_audio_done = true;
  }

  for (size_t i = 0; i < GB_DMG_PALETTE_COUNT; i++)
    s_pal[i] = to565be(GB_DMG_PALETTE[i]);
  for (int x = 0; x < GB_DST_W; x++)
    s_sx[x] = (uint8_t)(x * LCD_WIDTH / GB_DST_W);
  for (int y = 0; y < GB_DST_H; y++)
    s_sy[y] = (uint8_t)(y * LCD_HEIGHT / GB_DST_H);

  lvgl_glue_lock(-1);
  lvgl_glue_direct_begin();
  direct_started = true;
  esp_lcd_panel_swap_xy(panel_handle, true);
  esp_lcd_panel_mirror(panel_handle, true, false);
  panel_clear_black();
  ESP_LOGW(TAG,
           "running. OK=A BACK=B; both=A+B; RIGHT+both=START LEFT+both=SELECT; "
           "hold both 2s with D-pad released to exit.");

  int64_t start = esp_timer_get_time();
  uint64_t frame = 0;
  int64_t last_blit = 0;
  for (;;) {
    poll_input();
    if (s_exit_req)
      break;

    int64_t now = esp_timer_get_time();
    int64_t due = start + (int64_t)frame * GB_FRAME_US;
    bool behind = (now > due + GB_FRAME_US);
    bool want_blit = !behind && (now - last_blit >= 2 * GB_FRAME_US);
    s_gb->direct.frame_skip = !want_blit;

    if (setjmp(s_core_error_jmp)) {
      s_core_error_armed = false;
      break;
    }
    s_core_error_armed = true;
    gb_run_frame(s_gb);
    s_core_error_armed = false;
    frame++;

    if (want_blit) {
      blit_frame();
      last_blit = esp_timer_get_time();
    }
    ui_render_beat_kick();

    if (s_cram_dirty) {
      int64_t ms = esp_timer_get_time() / 1000;
      if (ms - s_cram_dirty_ms >= GB_AUTOSAVE_MS && !save_cram())
        break;
    }

    now = esp_timer_get_time();
    due = start + (int64_t)frame * GB_FRAME_US;
    if (due > now) {
      int64_t d_ms = (due - now) / 1000;
      if (d_ms >= 1)
        vTaskDelay(pdMS_TO_TICKS(d_ms));
    } else if (now - due > GB_RESYNC_US) {
      start = now - (int64_t)frame * GB_FRAME_US;
    }
  }

teardown:
  s_core_error_armed = false;
  if (f)
    fclose(f);
  ESP_LOGW(TAG, "returning to launcher%s", s_last_error[0] ? " after error" : "");
  if (save_ready && !s_last_error[0] && s_cram_dirty)
    save_cram();

  s_audio_run = false;
  for (int i = 0; i < GB_AUDIO_STOP_TRIES && !s_audio_done; i++)
    vTaskDelay(pdMS_TO_TICKS(GB_AUDIO_POLL_MS));
  if (s_apu)
    audio_i2s_stream_stop();

  vTaskDelay(pdMS_TO_TICKS(GB_DMA_DRAIN_MS));

  /* Never free APU memory while a stalled audio worker may still use it. */
  if (s_apu && s_audio_done) {
    free(s_apu);
    s_apu = NULL;
  }
  if (s_cram) {
    free(s_cram);
    s_cram = NULL;
  }
  if (s_strip) {
    free(s_strip);
    s_strip = NULL;
  }
  if (s_shade) {
    free(s_shade);
    s_shade = NULL;
  }
  if (s_gb) {
    free(s_gb);
    s_gb = NULL;
  }
  if (s_rom) {
    free(s_rom);
    s_rom = NULL;
  }

  if (direct_started) {
    drain_menu_input();
    lvgl_glue_direct_end();
    lvgl_glue_unlock();
  }
  s_has_save = false;
  s_rom_size = s_cram_size = 0;
  s_strip_rows = 0;
  s_running = false;
  s_finished = true;
  vTaskDeleteWithCaps(NULL);
}

void highboy_gb_start(const char *rompath) {
  if (s_running)
    return;
  s_last_error[0] = '\0';
  if (!s_audio_done) {
    set_error("Game audio is still stopping; try again");
    s_finished = true;
    return;
  }
  if (s_apu) {
    free(s_apu);
    s_apu = NULL;
  }
  if (rompath && strlen(rompath) >= sizeof(s_rompath)) {
    set_error("ROM path is too long");
    s_finished = true;
    return;
  }
  s_exit_req = false;
  highboy_gb_input_reset(&s_input_state);
  s_finished = false;
  s_running = true;
  s_audio_run = false;
  s_audio_done = true;
  if (rompath && rompath[0]) {
    strncpy(s_rompath, rompath, sizeof(s_rompath) - 1);
    s_rompath[sizeof(s_rompath) - 1] = '\0';
  } else {
    s_rompath[0] = '\0';
  }
  BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(gb_main_task,
                                                  "gameboy",
                                                  GB_MAIN_STACK,
                                                  NULL,
                                                  SYS_PRIO_SERVICE_HI,
                                                  NULL,
                                                  SYS_CORE_UI,
                                                  MALLOC_CAP_SPIRAM);
  ESP_LOGW(TAG, "highboy_gb_start: task create -> %s", ok == pdPASS ? "OK" : "FAILED");
  if (ok != pdPASS) {
    set_error("Not enough memory to start Game Boy");
    s_running = false;
    s_finished = true;
  }
}
