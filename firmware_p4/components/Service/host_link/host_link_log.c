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

// P4 log tee. Hooks esp_log_set_vprintf so every ESP_LOGx line is (1) still
// printed on the local dev console via the original handler, and (2) copied
// (ANSI stripped) into a drop-oldest ring. A worker task drains the ring and
// forwards each line as a LOG frame with source=P4. The hook never blocks and
// never logs, so it can't stall or recurse on the logging path.

#include "host_link.h"
#include "host_link_sec.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sys_prio.h"

#define HOST_LOG_LINE_MAX    240  // bytes of stripped text kept per line
#define HOST_LOG_QUEUE_DEPTH 24   // ring slots (drop-oldest beyond this)
#define HOST_LOG_TASK_STK    6144 // emit -> ble_write -> SPI command chain is deep
#define HOST_LOG_TASK_PRIO   SYS_PRIO_BACKGROUND

typedef struct {
  uint8_t level;
  uint16_t len;
  char text[HOST_LOG_LINE_MAX];
} log_line_t;

static QueueHandle_t s_log_queue = NULL;
static TaskHandle_t s_log_task = NULL;
static vprintf_like_t s_prev_vprintf = NULL;
static volatile uint32_t s_dropped = 0;

// Map the leading ESP-IDF level letter to the host-link level enum.
static host_log_level_t level_from_letter(char c) {
  switch (c) {
    case 'E':
      return HOST_LOG_LEVEL_ERROR;
    case 'W':
      return HOST_LOG_LEVEL_WARN;
    case 'D':
      return HOST_LOG_LEVEL_DEBUG;
    case 'V':
      return HOST_LOG_LEVEL_VERBOSE;
    case 'I':
    default:
      return HOST_LOG_LEVEL_INFO;
  }
}

// Copy src→dst dropping CSI/ANSI escape sequences (ESC '[' ... final 0x40-0x7E)
// and trailing CR/LF. Returns the dst length.
static uint16_t strip_ansi(const char *src, int src_len, char *dst, uint16_t dst_cap) {
  uint16_t n = 0;
  for (int i = 0; i < src_len && n < dst_cap; i++) {
    char c = src[i];
    if (c == '\033') {
      i++; // skip '['
      while (i + 1 < src_len && !(src[i + 1] >= '@' && src[i + 1] <= '~'))
        i++;
      i++; // skip the final byte of the sequence
      continue;
    }
    dst[n++] = c;
  }
  while (n > 0 && (dst[n - 1] == '\n' || dst[n - 1] == '\r'))
    n--;
  return n;
}

// Render + strip + queue the relayed copy. Kept in its own non-inlined function
// so its ~700 B of line buffers live only on THIS frame, allocated only when a
// companion is actually listening. log_vprintf runs on every logging task's
// stack (some driver tasks are only 4 KB), so it must stay tiny on the common
// path or it overflows them just by being entered.
static __attribute__((noinline)) void queue_relay_line(const char *fmt, va_list args) {
  char raw[HOST_LOG_LINE_MAX * 2];
  int raw_len = vsnprintf(raw, sizeof(raw), fmt, args);
  if (raw_len <= 0)
    return;
  if (raw_len > (int)sizeof(raw) - 1)
    raw_len = (int)sizeof(raw) - 1;

  log_line_t line;
  line.len = strip_ansi(raw, raw_len, line.text, sizeof(line.text));
  if (line.len == 0)
    return;
  line.level = (uint8_t)level_from_letter(line.text[0]);

  if (xQueueSend(s_log_queue, &line, 0) != pdTRUE) {
    log_line_t discard;
    if (xQueueReceive(s_log_queue, &discard, 0) == pdTRUE)
      s_dropped++;
    xQueueSend(s_log_queue, &line, 0);
  }
}

static int log_vprintf(const char *fmt, va_list args) {
  // Re-entrancy guard. s_prev_vprintf writes to stdout, and the UART VFS on the
  // far side of that write can itself ESP_LOGx - driver errors, and especially
  // esp_console_new_repl_uart swapping stdout onto the driver-backed VFS while
  // the console comes up. That re-enters this hook on the SAME task and recurses
  // ~900 B per level until the stack is gone (seen as a stack protection fault
  // in console_task whose depth scaled 1:1 with the task's stack size).
  // Per-task via TLS, so a logging task never masks another task's logs.
  static __thread bool in_hook = false;
  if (in_hook)
    return 0;

  // A frame is being forwarded right now: the forward runs blocking SPI that can
  // log (timeouts), which would re-enter this hook on the forwarder's stack
  // (overflow) and amplify (forwarded log -> more SPI -> more timeout logs).
  // Suppress entirely until the forward completes.
  if (host_link_is_emitting())
    return 0;

  in_hook = true;

  // 1. Preserve the local dev console with an untouched copy of the args.
  int ret = 0;
  if (s_prev_vprintf != NULL) {
    va_list args_copy;
    va_copy(args_copy, args);
    ret = s_prev_vprintf(fmt, args_copy);
    va_end(args_copy);
  }

  // 2. Relay to the companion only when one is actually connected and
  // authenticated. Skipping this keeps this frame tiny on the common path (no
  // companion): the render buffers below would otherwise be reserved on entry
  // and overflow small driver tasks (TinyUSB / wifi_status) just by logging.
  if (s_log_queue != NULL && host_link_sec_is_authenticated())
    queue_relay_line(fmt, args);

  in_hook = false;
  return ret;
}

static void log_task(void *arg) {
  (void)arg;
  log_line_t line;
  for (;;) {
    if (xQueueReceive(s_log_queue, &line, portMAX_DELAY) == pdTRUE) {
      host_link_emit_log(HOST_LOG_SRC_P4, (host_log_level_t)line.level, line.text, line.len);
    }
  }
}

esp_err_t host_link_log_init(void) {
  if (s_log_queue != NULL)
    return ESP_OK; // already installed

  s_log_queue = xQueueCreate(HOST_LOG_QUEUE_DEPTH, sizeof(log_line_t));
  if (s_log_queue == NULL)
    return ESP_ERR_NO_MEM;

  if (xTaskCreatePinnedToCore(log_task,
                              "hl_log",
                              HOST_LOG_TASK_STK,
                              NULL,
                              HOST_LOG_TASK_PRIO,
                              &s_log_task,
                              SYS_CORE_RADIO) != pdPASS) {
    vQueueDelete(s_log_queue);
    s_log_queue = NULL;
    return ESP_FAIL;
  }

  s_prev_vprintf = esp_log_set_vprintf(log_vprintf);
  return ESP_OK;
}
