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
typedef int esp_err_t;
typedef void *TaskHandle_t;
typedef struct { uint8_t length; } spi_header_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_STATE, ESP_ERR_NOT_SUPPORTED, ESP_ERR_TIMEOUT, ESP_ERR_NO_MEM, ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_CRC };
enum { SPI_ID_SYSTEM_PING, SPI_ID_SYSTEM_PROTO_VERSION, SPI_ID_SYSTEM_VERSION };
#define SPI_PROTOCOL_VERSION 2
#define SPI_BRIDGE_MODE_POLL 0
#define SPI_OTA_TRANSPORT_SPI 0
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define FIRMWARE_VERSION "test"
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
static int64_t now = 10000000;
static bool alive = true, permitted = true, exclusive;
static int resets, lost, probes, begins, reconciles;
static esp_err_t ping_result, begin_result, reset_result, board_result;
static uint16_t peer_version = SPI_PROTOCOL_VERSION;
int64_t esp_timer_get_time(void) { return now; }
void vTaskDelay(int ticks) { now += (int64_t)ticks * 1000; }
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, int stack, void *arg,
                           int priority, TaskHandle_t *out, int core) { *out = (void *)1; return pdPASS; }
esp_err_t spi_bridge_master_init_mode(int mode) { return ESP_OK; }
esp_err_t board_service_init(void) { return ESP_OK; }
esp_err_t c5_flasher_init(void) { return ESP_OK; }
esp_err_t c5_flasher_update(const uint8_t *data, uint32_t size, uint8_t transport) { return ESP_OK; }
void led_signal_info(void) {}
void led_signal_error(void) {}
bool spi_bridge_is_alive(void) { return alive; }
void spi_bridge_set_alive(bool value) { alive = value; }
bool spi_bridge_recovery_permitted(void) { return permitted; }
bool spi_bridge_recovery_is_disabled(void) { return false; }
esp_err_t spi_bridge_recovery_begin(uint32_t timeout) {
  begins++;
  if (begin_result != ESP_OK) return begin_result;
  assert(!exclusive); exclusive = true; alive = false;
  return ESP_OK;
}
void spi_bridge_recovery_finish(bool value) { assert(exclusive); exclusive = false; alive = value; }
void spi_session_link_lost(void) { assert(exclusive); lost++; }
esp_err_t board_pins_reset_c5(void) { assert(exclusive && lost > resets); resets++; return reset_result; }
esp_err_t board_service_reconcile(void) { assert(exclusive); reconciles++; return board_result; }
esp_err_t spi_bridge_send_command(int id, const void *tx, uint8_t len, spi_header_t *hdr,
                                  uint8_t *out, size_t cap, uint32_t timeout) {
  if (id == SPI_ID_SYSTEM_PING) { probes++; hdr->length = 0; return ping_result; }
  if (id == SPI_ID_SYSTEM_PROTO_VERSION) {
    assert(cap >= sizeof(peer_version));
    memcpy(out, &peer_version, sizeof(peer_version)); hdr->length = sizeof(peer_version); return ESP_OK;
  }
  assert(id == SPI_ID_SYSTEM_VERSION); out[0] = 0; hdr->length = 0; return ESP_OK;
}
/* PRODUCTION */
static void tick(void) { now += C5_MONITOR_PERIOD_MS * C5_US_PER_MS; monitor_once(); }
int main(void) {
  tick(); assert(resets == 0 && probes == 1);
  ping_result = ESP_ERR_NO_MEM;
  tick(); tick(); tick(); assert(resets == 0 && s_failures == 0);
  ping_result = ESP_ERR_TIMEOUT;
  tick(); assert(resets == 0);
  ping_result = ESP_OK; tick(); assert(s_failures == 0);
  ping_result = ESP_ERR_TIMEOUT;
  tick(); tick(); assert(resets == 0);
  begin_result = ESP_ERR_TIMEOUT; tick(); assert(resets == 0 && begins == 1);
  begin_result = ESP_OK;
  permitted = false; tick(); assert(resets == 0 && s_failures == 0);
  permitted = true;
  tick(); tick(); tick(); assert(resets == 1 && alive && reconciles == 1);
  tick(); tick(); tick(); assert(resets == 1);
  now = s_next_reset_us;
  reset_result = ESP_FAIL; tick(); assert(resets == 2 && !alive && s_resets == 2);
  ping_result = ESP_OK; tick(); assert(alive && resets == 2);
  ping_result = ESP_ERR_TIMEOUT;
  for (int i = 0; i < 100; i++) tick();
  assert(resets == 2 && !alive); // C5 success never replenishes P4's budget.
  ping_result = ESP_OK; peer_version = 99;
  tick(); assert(!alive && resets == 2);
  peer_version = SPI_PROTOCOL_VERSION; board_result = ESP_ERR_NOT_SUPPORTED;
  tick(); assert(!alive && resets == 2);
  board_result = ESP_OK; tick(); assert(alive && resets == 2);
  assert(!exclusive);
  puts("PASS: P4 recovery threshold, drain refusal, maintenance, cooldown, lifetime budget, failed GPIO and renegotiation");
}
