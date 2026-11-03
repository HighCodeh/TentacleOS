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
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "spi_protocol.h"
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
static void log_mock(const char *tag, ...) {}
#define ESP_LOGI(tag, ...) log_mock(tag, __VA_ARGS__)
#define pdMS_TO_TICKS(ms) (ms)
#define pdTICKS_TO_MS(ms) (ms)
static int holds, commands, polls;
static bool busy, disabled, no_memory, no_status, reject_image, alive = true;
static int64_t now;
static esp_err_t command_result;
const char *esp_err_to_name(esp_err_t value) { return "test"; }
int64_t esp_timer_get_time(void) { return now; }
uint32_t xTaskGetTickCount(void) { return (uint32_t)(now / 1000); }
void vTaskDelay(uint32_t ticks) { now += (int64_t)ticks * 1000; }
esp_err_t spi_bridge_maintenance_begin(void) { if (busy) return ESP_ERR_INVALID_STATE; holds++; return ESP_OK; }
void spi_bridge_maintenance_end(void) { assert(holds > 0); holds--; }
void spi_bridge_recovery_disable(void) { disabled = true; }
void spi_bridge_set_alive(bool value) { alive = value; }
static bool exclusive, suspended, session_lost;
static int physical_calls;
static esp_err_t physical_result;
esp_err_t spi_bridge_recovery_begin(uint32_t timeout) {
  assert(timeout == 500);
  if (busy) return ESP_ERR_INVALID_STATE;
  exclusive = true; return ESP_OK;
}
void spi_session_link_lost(void) { assert(exclusive && disabled); session_lost = true; }
void spi_bridge_suspend(bool value) { assert(exclusive); suspended = value; }
esp_err_t board_pins_download_c5(void) {
  assert(exclusive && disabled && suspended && session_lost);
  physical_calls++; return physical_result;
}
void spi_bridge_recovery_finish(bool value) { assert(exclusive && !value); exclusive = false; alive = value; }
static FILE *missing_file(const char *path, const char *mode) { assert(holds == 1); return NULL; }
static void *allocate(size_t size) { assert(holds == 1); return no_memory ? NULL : malloc(size); }
esp_err_t spi_bridge_send_command(spi_id_t id, const uint8_t *tx, uint8_t len,
                                  spi_header_t *header, uint8_t *out, size_t cap, uint32_t timeout) {
  assert(holds == 1); commands++;
  if (id == SPI_ID_SYSTEM_OTA_STATUS) {
    if (no_status) return ESP_ERR_TIMEOUT;
    spi_ota_status_t status = {0};
    status.state = reject_image ? SPI_OTA_STATE_ERROR :
        polls++ < 2 ? SPI_OTA_STATE_READY : SPI_OTA_STATE_DONE;
    assert(cap == sizeof(status)); memcpy(out, &status, sizeof(status)); return ESP_OK;
  }
  return command_result;
}
#define fopen missing_file
#define malloc allocate
/* PRODUCTION */
#undef fopen
#undef malloc
static void clear(void) {
  assert(holds == 0); commands = polls = 0; now = 0;
  busy = disabled = no_memory = no_status = reject_image = false;
  command_result = ESP_OK; alive = true;
}
int main(void) {
  clear(); busy = true;
  assert(c5_flasher_enter_download_physical() == ESP_ERR_INVALID_STATE);
  assert(!disabled && !suspended && physical_calls == 0 && commands == 0);
  clear(); physical_result = ESP_FAIL;
  assert(c5_flasher_enter_download_physical() == ESP_FAIL);
  assert(disabled && suspended && !exclusive && !alive && commands == 0);
  clear(); physical_result = ESP_OK;
  assert(c5_flasher_enter_download_physical() == ESP_OK);
  assert(disabled && suspended && !exclusive && !alive && commands == 0);
  assert(physical_calls == 2);
  const uint8_t image[] = {1};
  clear(); busy = true;
  assert(c5_flasher_update(image, sizeof(image), SPI_OTA_TRANSPORT_SPI) == ESP_ERR_INVALID_STATE);
  assert(commands == 0 && holds == 0);
  clear(); assert(c5_flasher_update(NULL, 0, SPI_OTA_TRANSPORT_SPI) == ESP_ERR_NOT_FOUND);
  assert(!disabled && holds == 0 && commands == 0);
  clear(); assert(c5_flasher_update(image, 0, SPI_OTA_TRANSPORT_SPI) == ESP_ERR_INVALID_ARG);
  assert(!disabled && holds == 0);
  clear(); no_memory = true;
  assert(c5_flasher_update(image, sizeof(image), SPI_OTA_TRANSPORT_SPI) == ESP_ERR_NO_MEM);
  assert(!disabled && holds == 0 && commands == 0);
  clear(); no_status = true;
  assert(c5_flasher_update(image, sizeof(image), SPI_OTA_TRANSPORT_SPI) != ESP_OK);
  assert(disabled && holds == 0 && commands > 0);
  clear(); reject_image = true;
  assert(c5_flasher_update(image, sizeof(image), SPI_OTA_TRANSPORT_SPI) != ESP_OK);
  assert(!disabled && holds == 0);
  clear(); assert(c5_flasher_update(image, sizeof(image), SPI_OTA_TRANSPORT_SPI) == ESP_OK);
  assert(!disabled && holds == 0 && !alive);
  clear(); command_result = ESP_ERR_TIMEOUT;
  assert(c5_flasher_enter_download() == ESP_OK && disabled && holds == 0);
  clear(); command_result = ESP_FAIL;
  assert(c5_flasher_enter_download() == ESP_FAIL && !disabled && holds == 0);
  clear(); assert(c5_flasher_reboot() == ESP_OK && !disabled && holds == 0);
  puts("PASS: real C5 updater recovery exclusion, preflight cleanup, uncertain BEGIN, terminal states and deliberate ROM/reboot");
}
