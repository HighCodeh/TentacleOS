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

// Bare-metal C5 flasher: the P4 talks the ESP ROM serial-bootloader protocol
// directly to the C5 over UART1 (via esp-serial-flasher), writing a full image
// set (bootloader + partition table + otadata + app) to a blank/bricked C5 -
// no PC and no esptool. The OTA path in c5_flasher.c stays the fast option for
// a C5 that is still running its app; this is the recovery path.

#include "c5_flasher.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_def.h"

#if C5_ROM_IMAGES_EMBEDDED
#include "esp32_port.h"
#include "esp_loader.h"
#endif

static const char *TAG = "C5_ROM";

#if !C5_ROM_IMAGES_EMBEDDED

esp_err_t c5_flasher_rom_flash(void) {
  ESP_LOGE(TAG, "C5 ROM images not embedded - build firmware_c5 and rebuild the P4");
  return ESP_ERR_NOT_FOUND;
}

#else // C5_ROM_IMAGES_EMBEDDED

// Images embedded by components/Service/CMakeLists.txt (target_add_binary_data).
// Symbol names follow the binary basename, with non-identifier chars -> '_'.
extern const uint8_t c5_app_start[] asm("_binary_TentacleOS_C5_bin_start");
extern const uint8_t c5_app_end[] asm("_binary_TentacleOS_C5_bin_end");
extern const uint8_t c5_bl_start[] asm("_binary_bootloader_bin_start");
extern const uint8_t c5_bl_end[] asm("_binary_bootloader_bin_end");
extern const uint8_t c5_pt_start[] asm("_binary_partition_table_bin_start");
extern const uint8_t c5_pt_end[] asm("_binary_partition_table_bin_end");
extern const uint8_t c5_otad_start[] asm("_binary_ota_data_initial_bin_start");
extern const uint8_t c5_otad_end[] asm("_binary_ota_data_initial_bin_end");

#define C5_ROM_UART  UART_NUM_1
// The C5 on the V2 board has a 48 MHz crystal, so its ROM bootloader runs at an
// effective baud of 115200 * 48/40 = 138240. 115200 produces garbled framing
// ("Invalid head of packet"); 138240 connects cleanly. (The OTA path stays at
// 115200 - it talks to the C5 app, which sets up its UART correctly.)
#define C5_ROM_BAUD  138240
// ROM (stubless) accepts modest data blocks; 1024 is the safe, well-tested size.
#define C5_ROM_BLOCK 1024

// The stock esp32_uart_ops configures reset_pin/boot_pin as outputs in init()
// and toggles them to drive the target into download mode. The V2 board has NO
// P4->C5 reset/boot line, so we run a custom ops table that reuses
// esp32_uart_ops for everything EXCEPT init (UART-only, no GPIO) and
// enter_bootloader/reset_target (no-ops). Nothing but TX/RX is ever touched;
// the C5 is placed into download mode over SPI (or by hand) beforehand.

static esp_loader_error_t rom_uart_only_init(esp_loader_port_t *port) {
  esp32_port_t *p = container_of(port, esp32_port_t, port);
  const uart_config_t cfg = {
      .baud_rate = (int)p->baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  if (uart_param_config((uart_port_t)p->uart_port, &cfg) != ESP_OK)
    return ESP_LOADER_ERROR_FAIL;
  if (uart_set_pin((uart_port_t)p->uart_port,
                   (int)p->uart_tx_pin,
                   (int)p->uart_rx_pin,
                   UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK)
    return ESP_LOADER_ERROR_FAIL;
  uint32_t rx = p->rx_buffer_size ? p->rx_buffer_size : 2048;
  if (uart_driver_install((uart_port_t)p->uart_port, rx, 0, 0, NULL, 0) != ESP_OK)
    return ESP_LOADER_ERROR_FAIL;
  p->_peripheral_needs_deinit = true; // let esp32_port_deinit() free the driver
  return ESP_LOADER_SUCCESS;
}

static void rom_noop_port(esp_loader_port_t *port) {
  (void)port;
}

// Initialized at runtime from esp32_uart_ops, then three callbacks overridden.
static esp_loader_port_ops_t s_rom_ops;

// C5 flash layout - must match firmware_c5/build/flasher_args.json.
typedef struct {
  uint32_t offset;
  const uint8_t *data;
  uint32_t size;
  const char *name;
} c5_region_t;

// Flash one region, padding the tail up to a 4-byte boundary with 0xFF (the
// ROM requires 4-byte-aligned image sizes). MD5 is verified by
// esp_loader_flash_finish().
static esp_err_t flash_one_region(esp_loader_t *loader, const c5_region_t *r) {
  static uint8_t block[C5_ROM_BLOCK];
  const uint32_t padded = (r->size + 3u) & ~3u;

  esp_loader_flash_cfg_t cfg = {
      .offset = r->offset,
      .image_size = padded,
      .block_size = C5_ROM_BLOCK,
      .skip_verify = false,
  };
  ESP_LOGI(TAG,
           "  '%s' @ 0x%05lX: %lu bytes",
           r->name,
           (unsigned long)r->offset,
           (unsigned long)r->size);

  esp_loader_error_t e = esp_loader_flash_start(loader, &cfg);
  if (e != ESP_LOADER_SUCCESS) {
    ESP_LOGE(TAG, "  flash_start('%s') failed: loader err %d", r->name, (int)e);
    return ESP_FAIL;
  }

  uint32_t off = 0;
  while (off < padded) {
    uint32_t n = (padded - off > C5_ROM_BLOCK) ? C5_ROM_BLOCK : (padded - off);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t src = off + i;
      block[i] = (src < r->size) ? r->data[src] : 0xFF; // 0xFF pad past EOF
    }
    e = esp_loader_flash_write(loader, &cfg, block, n);
    if (e != ESP_LOADER_SUCCESS) {
      ESP_LOGE(TAG,
               "  flash_write('%s') @ %lu failed: loader err %d",
               r->name,
               (unsigned long)off,
               (int)e);
      return ESP_FAIL;
    }
    off += n;
    if ((off & 0x3FFFF) < C5_ROM_BLOCK || off == padded)
      ESP_LOGI(TAG,
               "    %s %lu/%lu (%lu%%)",
               r->name,
               (unsigned long)off,
               (unsigned long)padded,
               (unsigned long)((uint64_t)off * 100 / padded));
  }

  e = esp_loader_flash_finish(loader, &cfg); // sends flash-end + verifies MD5
  if (e != ESP_LOADER_SUCCESS) {
    ESP_LOGE(TAG,
             "  flash_finish('%s') failed (MD5 mismatch / timeout): loader err %d",
             r->name,
             (int)e);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "  '%s' OK (verified)", r->name);
  return ESP_OK;
}

esp_err_t c5_flasher_rom_flash(void) {
  // esp-serial-flasher installs its own UART1 driver. If the OTA path left the
  // driver installed, remove it first so the loader port can own it cleanly.
  if (uart_is_driver_installed(C5_ROM_UART))
    uart_driver_delete(C5_ROM_UART);

  // Build our GPIO-safe ops: reuse the stock UART read/write/timer/log, but
  // replace init + reset/boot toggles so nothing outside TX/RX is ever driven.
  s_rom_ops = esp32_uart_ops;
  s_rom_ops.init = rom_uart_only_init;
  s_rom_ops.enter_bootloader = rom_noop_port;
  s_rom_ops.reset_target = rom_noop_port;

  esp32_port_t port = {
      .port.ops = &s_rom_ops,
      .baud_rate = C5_ROM_BAUD,
      .uart_port = C5_ROM_UART,
      .uart_rx_pin = GPIO_C5_UART_RX_PIN,
      .uart_tx_pin = GPIO_C5_UART_TX_PIN,
      .reset_pin = GPIO_NUM_NC, // unused - rom ops never touch reset/boot
      .boot_pin = GPIO_NUM_NC,
      .rx_buffer_size = 0,
      .tx_buffer_size = 0,
      .queue_size = 0,
      .uart_queue = NULL,
      .dont_initialize_peripheral = false,
  };

  esp_loader_t loader;
  esp_loader_error_t e = esp_loader_init_serial(&loader, &port.port);
  if (e != ESP_LOADER_SUCCESS) {
    ESP_LOGE(TAG, "esp_loader_init_serial failed: loader err %d", (int)e);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG,
           "Connecting to C5 ROM bootloader on UART%d @ %d (TX=GPIO%d/RX=GPIO%d)...",
           C5_ROM_UART,
           C5_ROM_BAUD,
           GPIO_C5_UART_TX_PIN,
           GPIO_C5_UART_RX_PIN);

  esp_loader_connect_args_t cargs = ESP_LOADER_CONNECT_DEFAULT();
  e = esp_loader_connect(&loader, &cargs);
  if (e != ESP_LOADER_SUCCESS) {
    ESP_LOGE(TAG, "connect failed: loader err %d", (int)e);
    ESP_LOGE(TAG, "  is the C5 in ROM download mode? (call c5_flasher_enter_download() first,");
    ESP_LOGE(TAG, "  or strap C5 GPIO28->GND and power-cycle it)");
    esp_loader_deinit(&loader);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG,
           "Connected. target_chip id=%d (ESP32C5 expected = %d)",
           (int)esp_loader_get_target(&loader),
           (int)ESP32C5_CHIP);

  const c5_region_t regions[] = {
      {0x2000, c5_bl_start, (uint32_t)(c5_bl_end - c5_bl_start), "bootloader"},
      {0x8000, c5_pt_start, (uint32_t)(c5_pt_end - c5_pt_start), "partition-table"},
      {0xf000, c5_otad_start, (uint32_t)(c5_otad_end - c5_otad_start), "otadata"},
      {0x20000, c5_app_start, (uint32_t)(c5_app_end - c5_app_start), "app"},
  };

  esp_err_t r = ESP_OK;
  uint32_t t0 = xTaskGetTickCount();
  for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
    r = flash_one_region(&loader, &regions[i]);
    if (r != ESP_OK)
      break;
  }

  if (r == ESP_OK) {
    uint32_t ms = pdTICKS_TO_MS(xTaskGetTickCount() - t0);
    ESP_LOGI(TAG, "C5 flashed + verified in %lu ms.", (unsigned long)ms);
    ESP_LOGI(TAG, "Power-cycle the C5 (no reset line on V2) to boot the new firmware.");
    esp_loader_reset_target(&loader); // no-op without a real reset line - harmless
  }

  esp_loader_deinit(&loader); // releases the UART1 driver
  return r;
}

#endif // C5_ROM_IMAGES_EMBEDDED
