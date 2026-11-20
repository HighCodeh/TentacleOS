#!/usr/bin/env python3
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

"""Exercise production PHY heap admission without hardware."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
s = (ROOT / "firmware_p4/components/Drivers/spi_bridge_phy/spi_bridge_phy.c").read_text()
start = s.index("esp_err_t spi_bridge_phy_transmit(")
end = s.index("esp_err_t spi_bridge_phy_legacy_begin(", start)
code = s[start:end]
fakes = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef int esp_err_t;
typedef void *spi_device_handle_t;
typedef struct {unsigned flags;size_t length;const void *tx_buffer;void *rx_buffer;} spi_transaction_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define SPI_DEVICE_BRIDGE 0
#define SPI_BRIDGE_DMA_MARGIN 1024
#define SPI_BRIDGE_DMA_ALIGNMENT 4
#define SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL 2048
#define MALLOC_CAP_DMA 8
#define MALLOC_CAP_SPIRAM 16
#define MALLOC_CAP_INTERNAL 32
#define SPI_TRANS_DMA_USE_PSRAM 4096
#define BRIDGE_CS_IDLE_US 100
static bool s_have_completion;
static int64_t s_last_completion_us;
static unsigned calls, queries, flags;
static size_t free_heap, cache_line;
static size_t external_heap=65536;
static bool dma=true, external;
static bool esp_ptr_external_ram(const void *p) {(void)p;return external;}
static void *spi_get_handle(int id) {(void)id;return (void *)1;}
static size_t esp_cache_get_line_size_by_addr(const void *p) {(void)p;return cache_line;}
static bool esp_ptr_dma_capable(const void *p) {(void)p;return dma;}
static size_t heap_caps_get_largest_free_block(int caps) {
 queries++;
 // IDF heap queries do not normalize allocation-only DMA | SPIRAM capabilities.
 if ((caps & MALLOC_CAP_SPIRAM) && (caps & MALLOC_CAP_DMA)) return 0;
 return (caps & MALLOC_CAP_SPIRAM) ? external_heap : free_heap;
}
static int64_t esp_timer_get_time(void) {return 1000;}
static void esp_rom_delay_us(uint32_t us) {(void)us;}
static int spi_device_polling_transmit(void *h,spi_transaction_t *t) {
 assert(h);calls++;flags=t->flags;return ESP_OK;
}
"""
tests = r"""
int main(void) {
 _Alignas(64) uint8_t tx[2112]={0},rx[2112]={0};
 assert(spi_bridge_phy_transmit(tx,rx,2080)==ESP_OK);
 assert(calls==1 && queries==0 && flags==SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL);
 assert(spi_bridge_phy_transmit(tx,NULL,296)==ESP_OK && queries==0);
 dma=false;assert(spi_bridge_phy_transmit(tx,rx,2080)==ESP_ERR_NO_MEM);
 assert(calls==2);free_heap=8192;
 assert(spi_bridge_phy_transmit(tx,rx,2080)==ESP_OK && flags==0);
 dma=true;free_heap=0;
 assert(spi_bridge_phy_transmit(tx+1,rx,296)==ESP_ERR_NO_MEM);
 cache_line=64;
 assert(spi_bridge_phy_transmit(tx,rx,2080)==ESP_ERR_NO_MEM);
 assert(spi_bridge_phy_transmit(tx,rx,2048)==ESP_OK && flags==SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL);
 external=true;dma=false;free_heap=0;
 assert(spi_bridge_phy_transmit(tx,rx,2080)==ESP_OK && flags==SPI_TRANS_DMA_USE_PSRAM);
 assert(spi_bridge_phy_transmit(tx,NULL,296)==ESP_OK && flags==SPI_TRANS_DMA_USE_PSRAM);
 external_heap=0;
 unsigned before=calls;
 assert(spi_bridge_phy_transmit(tx,rx,2080)==ESP_ERR_NO_MEM && calls==before);
 assert(spi_bridge_phy_transmit(NULL,NULL,296)==ESP_ERR_INVALID_ARG);
 assert(spi_bridge_phy_transmit(tx,rx,0)==ESP_ERR_INVALID_ARG);
 puts("PASS: DMA transfer with zero free heap, bounce-buffer guard and cache alignment");
}
"""
with tempfile.TemporaryDirectory(prefix="spi-dma-") as tmp:
    path = Path(tmp)
    c = path / "test.c"
    c.write_text(fakes + code + tests)
    binary = path / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
