import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = readFileSync(path.join(root,
  'firmware_p4/components/Applications/doom/doomgeneric_highboy.c'), 'utf8').replaceAll('\r\n', '\n');

function functionSource(name) {
  const start = source.indexOf(name);
  assert.ok(start >= 0, `Missing production function ${name}`);
  const opening = source.indexOf('{', start);
  let depth = 1;
  let end = opening + 1;
  while (depth && end < source.length) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
    end++;
  }
  assert.equal(depth, 0);
  return source.slice(start, end);
}

test('DOOM keeps launch/input hygiene and acquires lock before direct mode', () => {
  assert.match(source, /lvgl_glue_lock\(-1\);\s*lvgl_glue_direct_begin\(\);/);
  assert.match(source, /bool wait_release = true;/);
  assert.match(source, /while \(input_get_event\(&discarded, 0\)\)/);
  assert.match(source, /input_consume_press\(b\)/);
  assert.match(source, /if \(up && dn\) up = dn = false;/);
  assert.match(source, /if \(l && r\) l = r = false;/);
  assert.doesNotMatch(source, /s_fb\[|s_fb_sel/);
  assert.match(source, /if \(!strip_alloc\(\)\)[\s\S]*?esp_restart\(\);\s*return;/);
});

test('DOOM production DMA strip functions allocate, convert and wait safely', () => {
  // Exercise the production functions verbatim. SDK/task launch and filesystem
  // code are not needed to validate this pure strip path on the host.
  const functions = [
    'static inline uint16_t argb_to_565be(', 'static bool strip_alloc(',
    'static void panel_clear_black(', 'void DG_DrawFrame(',
  ].map(functionSource).join('\n\n');
  const defines = source.split('\n').filter(line =>
    /^#define (DG_W|DG_H|PANEL_W|PANEL_H|Y_OFF|STRIP|FLUSH_WAIT_MS)\s/.test(line)).join('\n');
  const harness = `
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200
${defines}
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
#define ESP_LOGE(tag, ...) ((void)(tag))
static const char *TAG = "DOOM_TEST";
static uint16_t *s_strip;
static int s_strip_rows;
static uint32_t pixels[DG_W * DG_H];
static void *DG_ScreenBuffer = pixels;
static void *panel_handle;
static size_t largest_block;
static int allocation_failures, allocations, draws, waits, next_y;
static bool pending, clearing;
size_t heap_caps_get_largest_free_block(unsigned caps) {
  assert(caps == (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  return largest_block;
}
void *heap_caps_malloc(size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  allocations++;
  if (allocation_failures > 0) { allocation_failures--; return NULL; }
  return size <= largest_block ? malloc(size) : NULL;
}
int esp_lcd_panel_draw_bitmap(void *panel, int x1, int y1, int x2, int y2,
                              const void *buffer) {
  (void)panel;
  assert(!pending && buffer == s_strip && x1 == 0 && x2 == PANEL_W);
  assert(y1 == next_y && y2 > y1 && y2 - y1 <= s_strip_rows);
  const uint16_t *data = buffer;
  for (int y = y1; y < y2; y++) {
    for (int x = 0; x < PANEL_W; x++) {
      uint16_t expected = 0;
      if (!clearing) {
        uint32_t color = pixels[(y - Y_OFF) * DG_W + x];
        uint16_t rgb = (uint16_t)(((color >> 8) & 0xF800) |
                                  ((color >> 5) & 0x07E0) | ((color >> 3) & 0x1F));
        expected = (uint16_t)((rgb << 8) | (rgb >> 8));
      }
      assert(data[(y - y1) * PANEL_W + x] == expected);
    }
  }
  next_y = y2;
  pending = true;
  draws++;
  return 0;
}
void lvgl_glue_wait_flush(uint32_t timeout_ms) {
  assert(timeout_ms == FLUSH_WAIT_MS && pending);
  pending = false;
  waits++;
}
${functions}
static void reset(void) {
  assert(!pending);
  free(s_strip);
  s_strip = NULL;
  s_strip_rows = 0;
  allocation_failures = allocations = draws = waits = next_y = 0;
  clearing = false;
}
int main(void) {
  largest_block = PANEL_W * STRIP * sizeof(uint16_t);
  assert(strip_alloc() && s_strip_rows == STRIP && allocations == 1);
  assert(argb_to_565be(0x00FF0000) == 0x00F8);
  assert(argb_to_565be(0x0000FF00) == 0xE007);
  assert(argb_to_565be(0x000000FF) == 0x1F00);
  for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++)
    pixels[i] = (uint32_t)(i * 173u);
  next_y = Y_OFF;
  DG_DrawFrame();
  assert(next_y == Y_OFF + DG_H && draws == 10 && waits == draws && !pending);
  clearing = true;
  draws = waits = next_y = 0;
  panel_clear_black();
  assert(next_y == PANEL_H && draws == 12 && waits == draws);
  reset();
  largest_block = PANEL_W * 3 * sizeof(uint16_t);
  assert(strip_alloc() && s_strip_rows == 2);
  reset();
  largest_block = PANEL_W * STRIP * sizeof(uint16_t);
  allocation_failures = 2;
  assert(strip_alloc() && s_strip_rows == 5 && allocations == 3);
  reset();
  largest_block = PANEL_W * sizeof(uint16_t);
  assert(strip_alloc() && s_strip_rows == 1);
  reset();
  largest_block = PANEL_W * sizeof(uint16_t) - 1;
  assert(!strip_alloc() && s_strip == NULL && s_strip_rows == 0 && allocations == 1);
  reset();
  largest_block = PANEL_W * STRIP * sizeof(uint16_t);
  assert(strip_alloc());
  s_strip_rows = 3; // Exercise the final partial strip, too.
  next_y = Y_OFF;
  DG_DrawFrame();
  assert(next_y == Y_OFF + DG_H && draws == 67 && waits == draws);
  reset();
  puts("PASS: DOOM allocation fallback, RGB565, full/partial strips, draw/wait ordering");
  return 0;
}
`;
  const temporary = mkdtempSync(path.join(tmpdir(), 'highboy-doom-test-'));
  try {
    const harnessPath = path.join(temporary, 'doom_dma_test.c');
    const executable = path.join(temporary, 'doom_dma_test');
    writeFileSync(harnessPath, harness);
    const compiled = spawnSync(process.env.CC || 'cc', [
      '-std=c11', '-Wall', '-Wextra', '-Werror', harnessPath, '-o', executable,
    ], { encoding: 'utf8' });
    assert.equal(compiled.status, 0, compiled.error?.message || compiled.stderr);
    const result = spawnSync(executable, [], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.error?.message || result.stderr);
    assert.match(result.stdout, /PASS: DOOM allocation fallback/);
    console.log(result.stdout.trim());
  } finally {
    rmSync(temporary, { recursive: true, force: true });
  }
});
