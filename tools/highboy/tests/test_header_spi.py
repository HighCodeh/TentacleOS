# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Exercise production header SPI ownership, software CS and failure cleanup."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / "firmware_p4/components/Drivers/header_spi/header_spi.c").read_text()
source = re.sub(r'^#include .*$', '', source, flags=re.M)
fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
typedef void *spi_device_handle_t;
typedef struct { uint32_t clock_hz; uint8_t cs_pin, control_pin, mode; } header_spi_config_t;
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
typedef struct { int clock_speed_hz, mode, spics_io_num, queue_size; } spi_device_interface_config_t;
typedef struct { size_t length; const void *tx_buffer; void *rx_buffer; } spi_transaction_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_NO_MEM 4
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_USB_DEBUG_DM_PIN 24
#define GPIO_USB_DEBUG_DP_PIN 25
#define HEADER_SPI_DEBUG_DM_PIN 7
#define HEADER_SPI_DEBUG_DP_PIN 9
#define HEADER_SPI_TRANSFER_MAX 64
#define SPI3_HOST 2
#define DMA_ATTR
#define portMAX_DELAY UINT32_MAX
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
static bool locked, owned, hold, cs_high = true;
static int add_error, transfer_error, acquire_error, remove_error;
static bool spi_bus_lock_take(uint32_t timeout) { assert(timeout && !locked); locked=true; return true; }
static void spi_bus_lock_give(void) { assert(locked && !owned); locked=false; }
static int power_manager_no_sleep_acquire(void) { assert(!hold); hold=true; return 0; }
static int power_manager_no_sleep_release(void) { assert(hold); hold=false; return 0; }
static int gpio_set_level(int pin, int level) {
  if (pin == 25) { if (!level) assert(locked && owned); cs_high=level; }
  return 0;
}
static int gpio_config(const gpio_config_t *config) { assert(config); return 0; }
static int spi_bus_add_device(int host, const spi_device_interface_config_t *config, void **out) {
  assert(host == 2 && locked && hold && cs_high);
  /* Hardware CS slots are occupied by internal peripherals. */
  if (config->spics_io_num != -1) return ESP_ERR_NO_MEM;
  if (add_error) return add_error;
  *out=(void *)1; return 0;
}
static int spi_device_acquire_bus(void *device, uint32_t wait) {
  assert(device && locked && !owned && cs_high && wait==portMAX_DELAY);
  if (acquire_error) return acquire_error;
  owned=true; return 0;
}
static int spi_device_polling_transmit(void *device, spi_transaction_t *transaction) {
  assert(device && locked && owned && !cs_high);
  memcpy(transaction->rx_buffer, transaction->tx_buffer, transaction->length/8);
  return transfer_error;
}
static void spi_device_release_bus(void *device) { assert(device && owned && cs_high); owned=false; }
static int spi_bus_remove_device(void *device) { assert(device && locked && cs_high); return remove_error; }
"""
checks = r"""
int main(void) {
  header_spi_config_t config={1000000,9,7,0};
  uint8_t tx[3]={1,2,3}, rx[3]={0};
  for (int i=0; i<100; i++) {
    assert(header_spi_open(&config)==0);
    assert(header_spi_transfer(tx,rx,sizeof(tx))==0);
    assert(memcmp(tx,rx,sizeof(tx))==0);
    assert(header_spi_close()==0);
    assert(!hold && !locked && !owned && cs_high);
  }
  add_error=ESP_ERR_NO_MEM;
  assert(header_spi_open(&config)==add_error);
  assert(!hold && !locked && !s_device);
  add_error=0;
  assert(header_spi_open(&config)==0);
  acquire_error=ESP_ERR_TIMEOUT;
  assert(header_spi_transfer(tx,rx,3)==acquire_error);
  assert(!locked && !owned && cs_high);
  acquire_error=0;
  transfer_error=ESP_ERR_TIMEOUT;
  assert(header_spi_transfer(tx,rx,3)==transfer_error);
  assert(!locked && !owned && cs_high);
  remove_error=ESP_ERR_INVALID_STATE;
  assert(header_spi_close()==remove_error && hold && s_device);
  remove_error=0;
  assert(header_spi_close()==0 && !hold && !s_device);
  assert(header_spi_close()==0);
  return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="header-spi-test-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(fixture + source + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(path / "test.c"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Header SPI lifecycle and software CS tests passed")
