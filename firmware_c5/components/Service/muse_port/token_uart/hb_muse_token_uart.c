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

#include "hb_muse_token_uart.h"
#include "hb_muse_token_frame.h"
#ifdef HB_MUSE_TOKEN_UART_HOST_TEST
#include "token_uart_host.h"
#else
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "sys_prio.h"
#endif
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static atomic_bool s_active;
static atomic_uint s_revision;
static unsigned s_armed_revision;
static int64_t s_last_time;
typedef struct {
  unsigned revision;
  int64_t started, deadline;
} copy_guard_t;
static hb_muse_token_store_fn s_store;
static bool (*s_quiet)(void);
static void wipe(void *p, size_t n) {
  volatile uint8_t *v = p;
  while (n--)
    *v++ = 0;
}
bool hb_muse_token_uart_active(void) {
  return atomic_load(&s_active);
}
void hb_muse_token_uart_cancel(void) {
  unsigned revision = atomic_load(&s_revision);
  while (revision != UINT32_MAX &&
         !atomic_compare_exchange_weak(&s_revision, &revision, revision + 1u)) {}
}
static bool current(int64_t deadline) {
  int64_t now = esp_timer_get_time();
  bool time_ok = now >= s_last_time && now >= 0 && now < deadline;
  s_last_time = now;
  return atomic_load(&s_revision) == s_armed_revision && time_ok && s_quiet && s_quiet();
}
static bool copy_current(void *user) {
  const copy_guard_t *guard = user;
  int64_t now = esp_timer_get_time();
  bool time_ok = now >= s_last_time && now >= guard->started && now < guard->deadline;
  s_last_time = now;
  return atomic_load(&s_revision) == guard->revision && time_ok;
}
static bool read_exact(uint8_t *p, size_t n, int64_t deadline) {
  size_t offset = 0;
  while (offset < n && current(deadline)) {
    int count = uart_read_bytes(UART_NUM_0, p + offset, n - offset, pdMS_TO_TICKS(20));
    if (count < 0 || (size_t)count > n - offset)
      return false;
    offset += (size_t)count;
  }
  return offset == n && current(deadline);
}
static void worker(void *unused) {
  (void)unused;
  bool installed = false, accepted = false;
  uint8_t nonce[16] = {0};
  uint8_t *frame =
      heap_caps_calloc(1, HB_MUSE_TOKEN_FRAME_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!frame)
    goto done;
  const uart_config_t config = {.baud_rate = 115200,
                                .data_bits = UART_DATA_8_BITS,
                                .parity = UART_PARITY_DISABLE,
                                .stop_bits = UART_STOP_BITS_1,
                                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                                .source_clk = UART_SCLK_DEFAULT};
  if (uart_is_driver_installed(UART_NUM_0) ||
      uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0) != ESP_OK)
    goto done;
  installed = true;
  if (uart_param_config(UART_NUM_0, &config) != ESP_OK ||
      uart_set_pin(UART_NUM_0, 11, 12, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
    goto done;
  uart_flush_input(UART_NUM_0);
  esp_fill_random(nonce, sizeof(nonce));
  int64_t started = esp_timer_get_time();
  if (started < 0 || started > INT64_MAX - INT64_C(120000000))
    goto done;
  s_last_time = started;
  const int64_t deadline = started + INT64_C(120000000);
  if (!current(deadline))
    goto done;
  /* Only non-secret, per-window challenge is printed. No RX echo. */
  char challenge[33];
  for (unsigned i = 0; i < 16; ++i)
    (void)snprintf(challenge + 2u * i, 3, "%02x", nonce[i]);
  printf("\nHB_MUSE_TOKEN C5 v1 nonce=%s max=1023 ttl=120\n", challenge);
  fflush(stdout);
  if (!read_exact(frame, HB_MUSE_TOKEN_HEADER, deadline))
    goto done;
  size_t bytes = (size_t)frame[21] | (size_t)frame[22] << 8;
  if (memcmp(frame, "HBMU1", 5) || memcmp(frame + 5, nonce, 16) || bytes > HB_MUSE_TOKEN_MAX ||
      bytes < 6)
    goto done;
  if (!read_exact(frame + HB_MUSE_TOKEN_HEADER, bytes + 4u, deadline))
    goto done;
  char *token = NULL;
  size_t length = 0;
  copy_guard_t copy_guard = {s_armed_revision, started, deadline};
  if (hb_muse_token_frame_decode(
          frame, HB_MUSE_TOKEN_HEADER + bytes + 4u, nonce, &token, &length) &&
      current(deadline))
    accepted = s_store(token, length, copy_current, &copy_guard);
done:
  if (frame) {
    wipe(frame, HB_MUSE_TOKEN_FRAME_MAX);
    heap_caps_free(frame);
  }
  wipe(nonce, sizeof(nonce));
  if (installed) {
    uart_flush_input(UART_NUM_0);
    int cleanup = uart_driver_delete(UART_NUM_0);
    /* IDF6 delete releases the console pins, but keeps its clock enabled.
     * Restore TX only before the ACK; RX must remain unclaimed/no echo. */
    int restore =
        uart_set_pin(UART_NUM_0, 11, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (cleanup != ESP_OK || restore != ESP_OK)
      printf("\nHB_MUSE_TOKEN C5 console_cleanup=%d restore=%d\n", cleanup, restore);
  }
  printf("\nHB_MUSE_TOKEN C5 result=%s\n", accepted ? "accepted_ram_only" : "not_loaded");
  fflush(stdout);
  s_store = NULL;
  s_quiet = NULL;
  atomic_store(&s_active, false);
  vTaskDeleteWithCaps(NULL);
}
bool hb_muse_token_uart_arm(hb_muse_token_store_fn store, bool (*quiet)(void)) {
  unsigned revision = atomic_load(&s_revision);
  if (revision == UINT32_MAX)
    return false;
  if (!store || !quiet || !quiet() || uart_is_driver_installed(UART_NUM_0))
    return false;
  bool expected = false;
  if (!atomic_compare_exchange_strong(&s_active, &expected, true))
    return false;
  s_armed_revision = revision;
  s_store = store;
  s_quiet = quiet;
  if (atomic_load(&s_revision) != revision ||
      xTaskCreatePinnedToCoreWithCaps(worker,
                                      "muse_token",
                                      4096,
                                      NULL,
                                      SYS_PRIO_BACKGROUND,
                                      NULL,
                                      SYS_CORE_MAIN,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
    s_store = NULL;
    s_quiet = NULL;
    atomic_store(&s_active, false);
    return false;
  }
  return true;
}
