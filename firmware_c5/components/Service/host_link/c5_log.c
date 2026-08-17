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

#include "c5_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "spi_bridge.h"
#include "spi_protocol.h"

#define C5_LOG_TEXT_MAX    240 // bytes of stripped text kept per line
#define C5_LOG_QUEUE_DEPTH 12
#define C5_LOG_TASK_STK    3072
#define C5_LOG_TASK_PRIO   SYS_PRIO_SERVICE_LO

typedef struct {
  uint8_t level;
  uint16_t len;
  char text[C5_LOG_TEXT_MAX];
} log_line_t;

static QueueHandle_t s_log_queue = NULL;
static TaskHandle_t s_log_task = NULL;
static vprintf_like_t s_prev_vprintf = NULL;
static volatile uint32_t s_dropped = 0;

// Level enum on the wire: matches the P4 host_link_level_t (E=0,W=1,I=2,D=3,V=4).
static uint8_t level_from_letter(char c) {
  switch (c) {
    case 'E':
      return 0;
    case 'W':
      return 1;
    case 'D':
      return 3;
    case 'V':
      return 4;
    case 'I':
    default:
      return 2;
  }
}

// Copy src→dst dropping CSI/ANSI escape sequences and trailing CR/LF.
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

static int log_vprintf(const char *fmt, va_list args) {
  int ret = 0;
  if (s_prev_vprintf != NULL) {
    va_list args_copy;
    va_copy(args_copy, args);
    ret = s_prev_vprintf(fmt, args_copy);
    va_end(args_copy);
  }

  if (s_log_queue == NULL)
    return ret;

  char raw[C5_LOG_TEXT_MAX * 2];
  int raw_len = vsnprintf(raw, sizeof(raw), fmt, args);
  if (raw_len <= 0)
    return ret;
  if (raw_len > (int)sizeof(raw) - 1)
    raw_len = (int)sizeof(raw) - 1;

  log_line_t line;
  line.len = strip_ansi(raw, raw_len, line.text, sizeof(line.text));
  if (line.len == 0)
    return ret;
  line.level = level_from_letter(line.text[0]);

  if (xQueueSend(s_log_queue, &line, 0) != pdTRUE) {
    log_line_t discard;
    if (xQueueReceive(s_log_queue, &discard, 0) == pdTRUE)
      s_dropped++;
    xQueueSend(s_log_queue, &line, 0);
  }
  return ret;
}

static void log_task(void *arg) {
  (void)arg;
  log_line_t line;
  uint8_t record[1 + C5_LOG_TEXT_MAX];
  for (;;) {
    if (xQueueReceive(s_log_queue, &line, portMAX_DELAY) != pdTRUE)
      continue;
    // Push to the P4 only when the stream is enabled (a companion is listening).
    if (!spi_bridge_stream_is_enabled(SPI_ID_SYSTEM_LOG))
      continue;
    record[0] = line.level;
    memcpy(record + 1, line.text, line.len);
    spi_bridge_stream_push(SPI_ID_SYSTEM_LOG, record, (uint8_t)(1 + line.len));
  }
}

esp_err_t c5_log_init(void) {
  if (s_log_queue != NULL)
    return ESP_OK; // already installed

  s_log_queue = xQueueCreate(C5_LOG_QUEUE_DEPTH, sizeof(log_line_t));
  if (s_log_queue == NULL)
    return ESP_ERR_NO_MEM;

  if (xTaskCreate(log_task, "c5_log", C5_LOG_TASK_STK, NULL, C5_LOG_TASK_PRIO, &s_log_task) !=
      pdPASS) {
    vQueueDelete(s_log_queue);
    s_log_queue = NULL;
    return ESP_FAIL;
  }

  // NimBLE logs every GATT/GAP procedure at INFO. Besides the noise, those lines
  // would be teed and forwarded to the companion over BLE, whose notify triggers
  // another NimBLE "notify" log -> an infinite feedback loop. Keep only warnings+.
  esp_log_level_set("NimBLE", ESP_LOG_WARN);

  spi_bridge_stream_enable(SPI_ID_SYSTEM_LOG, true);
  s_prev_vprintf = esp_log_set_vprintf(log_vprintf);
  return ESP_OK;
}
