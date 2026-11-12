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
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* CONSTANTS */
#define ESP_OK 0
#define HB_NFC_OK 0
#define HB_NFC_ERR_INTERNAL -1
#define RF_FRONTEND_NFC_13MHZ 1
#define HIGHBOY_NFC_CONFIG_DEFAULT() 0

typedef int esp_err_t;
typedef int hb_nfc_err_t;
typedef int highboy_nfc_config_t;
static bool s_has_reservation, s_has_power, s_has_emulator;
static int s_card;
static int response_mode, attempts, start_calls, failure;
static bool antcl, initialized, reset_called;
static int step;
static void set_antcl(bool value) { antcl = value; }
static int nfc_poller_transceive(const uint8_t *tx, size_t len, bool crc,
                                uint8_t *rx, size_t cap, size_t min, int timeout) {
  assert(tx[0] == 0x93 && tx[1] == 0x20 && len == 2 && !crc && antcl);
  assert(cap == 5 && min == 5 && timeout == 20);
  const uint8_t good[] = {0x88,0x04,0xA1,0xB2,0x9F};
  memcpy(rx, good, 5);
  attempts++;
  if (response_mode == 1 || (response_mode == 2 && attempts == 1)) rx[4] ^= 1;
  return response_mode == 3 ? 4 : 5;
}
static void hb_nfc_timer_delay_ms(int ms) { assert(ms == ANTICOLL_DELAY_MS); }
static void nfc_log_hex(const char *label, const uint8_t *data, size_t len) {
  (void)label; (void)data; assert(len == 5);
}
static int rf_frontend_reserve(int rf) { assert(rf == 1 && step++ == 0); return failure == 1 ? -1 : 0; }
static int power_manager_no_sleep_acquire(void) { assert(step++ == 1); return failure == 2 ? -1 : 0; }
static void highboy_nfc_deinit(void) { assert(step++ == 2); reset_called = true; initialized = failure == 3; }
static bool highboy_nfc_is_initialized(void) { return initialized; }
static int highboy_nfc_init(const int *cfg) { assert(*cfg == 0 && step++ == 3 && reset_called && !initialized); return failure == 4 ? -1 : 0; }
static int mfc_emu_init(const int *card) { assert(card == &s_card && step++ == 4); return failure == 5 ? -1 : 0; }
static int mfc_emu_configure_target(void) { assert(step++ == 5); return failure == 6 ? -1 : 0; }
static int mfc_emu_start(void) { assert(step++ == 6); start_calls++; return failure == 7 ? -1 : 0; }
static bool s_is_t2t, s_is_plus, s_t2t_read_only, s_t2t_loaded;
static struct { int config, memory, length; } s_t2t_card;
static int s_plus_identity, s_plus_listener;
static int t2t_emu_init(const int *c) { (void)c; assert(false); return -1; }
static int t2t_emu_load_memory(int m, int l, bool r) { (void)m;(void)l;(void)r;assert(false);return -1; }
static int t2t_emu_configure_target(void) { assert(false);return -1; }
static int t2t_emu_start(void) { assert(false);return -1; }
static int t4t_emu_init_plus(int *c,int *l) { (void)c;(void)l;assert(false);return -1; }
static int t4t_emu_configure_target(void) { assert(false);return -1; }
static int t4t_emu_start(void) { assert(false);return -1; }
/* PRODUCTION */

int main(void) {
  uint8_t uid[5];
  for (response_mode = 0; response_mode < 4; response_mode++) {
    attempts = 0;
    const int got = iso14443a_poller_anticoll(0x93, uid);
    assert(!antcl);
    if (response_mode == 0) assert(got == 5 && attempts == 1);
    if (response_mode == 1) assert(got == 0 && attempts == 3);
    if (response_mode == 2) assert(got == 5 && attempts == 2);
    if (response_mode == 3) assert(got == 0 && attempts == 3);
  }
  for (failure = 0; failure < 8; failure++) {
    step = start_calls = 0; initialized = true; reset_called = false;
    s_has_reservation = s_has_power = s_has_emulator = false;
    const int result = configure_hardware();
    assert((failure == 0) == (result == 0));
    if (failure && failure < 7) assert(start_calls == 0);
    if (failure > 3 || failure == 0) assert(reset_called);
    if (failure == 3) assert(initialized && !s_has_emulator);
  }
  puts("NFC activation: BCC retry/rejection and target reset/failure paths PASS (host only)");
  return 0;
}
