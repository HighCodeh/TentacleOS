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
#include <stdatomic.h>
#include <stdio.h>
typedef int esp_err_t;
typedef int spi_id_t;
enum {SPI_ID_SYSTEM_PING, SPI_ID_SYSTEM_VERSION, SPI_ID_SYSTEM_PROTO_VERSION, SPI_ID_BOARD_NEGOTIATE, SPI_ID_BOARD_STATE};
typedef void *TaskHandle_t;
enum { ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT };
static atomic_bool s_suspended, s_bridge_alive = true;
static esp_err_t barrier_result;
static uint64_t s_master_session = 123, s_slave_session = 456;
static uint32_t s_transaction = 9;
static bool s_transport_ready = true;
static int64_t s_recovery_quiet_until = 900, now;
static TaskHandle_t current = (void *)1;
static esp_err_t lock_result;
static bool idle = true, locked;
static int unlocks;
int64_t esp_timer_get_time(void) { return now; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current; }
esp_err_t spi_bridge_lock(uint32_t timeout) { if (lock_result == ESP_OK) locked = true; return lock_result; }
void spi_bridge_unlock(void) { assert(locked); locked = false; unlocks++; }
bool spi_bridge_phy_is_idle(void) { assert(locked); return idle; }
esp_err_t spi_bridge_stream_barrier(uint32_t timeout) { assert(!locked); return barrier_result; }
/* PRODUCTION */
int main(void) {
  assert(!spi_bridge_recovery_permitted());
  now = 10000000; assert(spi_bridge_recovery_permitted());
  assert(spi_bridge_maintenance_begin() == ESP_OK);
  assert(spi_bridge_maintenance_begin() == ESP_OK);
  assert(!spi_bridge_recovery_permitted() && recovery_allows_traffic());
  assert(spi_bridge_recovery_begin(500) == ESP_ERR_INVALID_STATE);
  spi_bridge_maintenance_end(); assert(!spi_bridge_recovery_permitted());
  spi_bridge_maintenance_end(); assert(!spi_bridge_recovery_permitted());
  now += 10000000; assert(spi_bridge_recovery_permitted());
  lock_result = ESP_ERR_TIMEOUT;
  assert(spi_bridge_recovery_begin(500) == ESP_ERR_TIMEOUT);
  assert(spi_bridge_link_generation() == 0 && s_transport_ready);
  lock_result = ESP_OK; idle = false;
  assert(spi_bridge_recovery_begin(500) == ESP_ERR_INVALID_STATE);
  assert(spi_bridge_link_generation() == 0 && !locked);
  idle = true;
  assert(spi_bridge_recovery_begin(500) == ESP_OK);
  assert(!locked && !s_bridge_alive && !s_transport_ready && s_master_session == 0 && s_slave_session == 0);
  assert(s_transaction == 0 && s_recovery_quiet_until == 0);
  assert(spi_bridge_link_generation() == 1);
  assert(spi_bridge_maintenance_begin() == ESP_ERR_INVALID_STATE);
  assert(recovery_allows_traffic());
  assert(recovery_allows_command(SPI_ID_BOARD_NEGOTIATE));
  assert(!recovery_allows_command(999));
  current = (void *)2;
  assert(!recovery_allows_traffic());
  spi_bridge_recovery_finish(true); assert(!s_bridge_alive);
  current = (void *)1;
  spi_bridge_recovery_finish(false); assert(!s_bridge_alive);
  assert(spi_bridge_recovery_permitted());
  assert(spi_bridge_recovery_begin(500) == ESP_OK);
  spi_bridge_recovery_finish(true); assert(s_bridge_alive);
  s_suspended = true;
  assert(spi_bridge_recovery_begin(500) == ESP_ERR_INVALID_STATE);
  assert(spi_bridge_link_generation() == 2);
  s_suspended = false;
  barrier_result = ESP_ERR_TIMEOUT;
  assert(spi_bridge_recovery_begin(500) == ESP_ERR_TIMEOUT);
  assert(atomic_load(&s_recovery_owner) == NULL && !locked);
  spi_bridge_recovery_disable();
  assert(!spi_bridge_recovery_permitted());
  assert(spi_bridge_recovery_begin(500) == ESP_ERR_INVALID_STATE);
  assert(spi_bridge_maintenance_begin() == ESP_OK);
  spi_bridge_maintenance_end();
  assert(!spi_bridge_recovery_permitted());
  puts("PASS: recovery exclusion, maintenance nesting, drain failure, CS idle gate, generation invalidation and owner-only completion");
}
