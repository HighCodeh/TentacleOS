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

#include "bridge_manager.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "c5_flasher.h"
#include "ota_version.h"
#include "spi_bridge.h"

static const char *TAG = "BRIDGE_MGR";

#define VERSION_BUF_SIZE   32
#define VERSION_TIMEOUT_MS 1000

// Link monitor: how often to re-probe the C5 while the bridge is marked dead,
// and the per-probe timeout. Lets the P4 pick up a C5 that booted late (or
// rebooted, e.g. after an OTA) instead of latching "no C5" forever.
#define C5_MONITOR_PERIOD_MS 1500
#define C5_PROBE_TIMEOUT_MS  500

// Background task: while the bridge is dead, keep probing; revive it the moment
// the C5 answers. Idle (no bus traffic) once the link is up, so it never
// competes with real commands.
static void c5_link_monitor(void *arg) {
  (void)arg;
  spi_header_t hdr;
  uint8_t ver[VERSION_BUF_SIZE];
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(C5_MONITOR_PERIOD_MS));
    if (spi_bridge_is_alive()) {
      continue; // link already up - don't poke the bus
    }
    // Optimistically allow one probe (nothing else touches the bus while dead).
    spi_bridge_set_alive(true);
    memset(ver, 0, sizeof(ver));
    esp_err_t r =
        spi_bridge_send_command(SPI_ID_SYSTEM_VERSION, NULL, 0, &hdr, ver, C5_PROBE_TIMEOUT_MS);
    if (r == ESP_OK) {
      ESP_LOGI(TAG, "C5 link established (detected after boot), version: %s", ver);
    } else {
      spi_bridge_set_alive(false); // still absent - stay dead, try again later
    }
  }
}

esp_err_t bridge_manager_init(void) {
  ESP_LOGI(TAG, "Initializing bridge manager");
  ESP_LOGI(TAG, "Expected C5 version: %s", FIRMWARE_VERSION);

  // The HighBoy V2 PCB has no bridge IRQ trace (GPIO_BRIDGE_IRQ_PIN = -1), so
  // the master polls the bus instead of waiting on an IRQ. Must match the C5.
  if (spi_bridge_master_init_mode(SPI_BRIDGE_MODE_POLL) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init SPI bridge");
    return ESP_FAIL;
  }

  // Background link monitor: idles while the bridge is alive, and re-detects the
  // C5 whenever it appears if the boot-time check marks the bridge dead (late
  // C5 boot, or a C5 reboot after an OTA). Started once here.
  // 6 KB: the probe calls spi_bridge_send_command (which puts two SPI_FRAME_SIZE
  // buffers on the stack) and logs via vfprintf on timeout - 3 KB overflowed.
  xTaskCreate(c5_link_monitor, "c5_link_mon", 6144, NULL, 4, NULL);

  spi_header_t resp_header;
  uint8_t resp_ver[VERSION_BUF_SIZE];
  memset(resp_ver, 0, sizeof(resp_ver));

  ESP_LOGI(TAG, "Checking C5 version");
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_SYSTEM_VERSION, NULL, 0, &resp_header, resp_ver, VERSION_TIMEOUT_MS);

  // Detection only - never flash automatically. If the C5 is silent or on a
  // different version, just report it; the user updates explicitly with the
  // 'c5' console command. The link monitor keeps re-probing while it is down.
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "C5 not responding - bridge marked down (run 'c5 ota' to flash the C5)");
    spi_bridge_set_alive(false);
    return ESP_OK;
  }

  ESP_LOGI(TAG, "C5 version: %s (expected: %s)", resp_ver, FIRMWARE_VERSION);
  if (strcmp((char *)resp_ver, FIRMWARE_VERSION) != 0) {
    ESP_LOGW(TAG, "C5 version differs - update available (run 'c5 ota' to apply)");
  }
  ESP_LOGI(TAG, "C5 bridge alive");
  return ESP_OK;
}

esp_err_t bridge_manager_force_update(void) {
  c5_flasher_init();
  return c5_flasher_update(NULL, 0);
}
