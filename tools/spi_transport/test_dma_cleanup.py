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

"""Exercise the generated IDF SPI cleanup with failed DMA allocation."""
from pathlib import Path
import subprocess
import tempfile
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/nfc_api"))
from test_emu_tx import extract
source = (ROOT / "firmware_p4/build/components/Drivers/esp_driver_spi/src/gpspi/spi_master.c").read_text()
code = extract(source, "static SPI_MASTER_ISR_ATTR void uninstall_priv_desc(")
fixture = """
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define SPI_MASTER_ISR_ATTR
#define SPI_TRANS_USE_RXDATA 1
typedef struct { int flags, rxlength; void *tx_buffer, *rx_buffer; uint8_t tx_data[4], rx_data[4]; } spi_transaction_t;
typedef struct { spi_transaction_t *trans; const void *buffer_to_send; void *buffer_to_rcv; } spi_trans_priv_t;
""" + code + """
int main(void) {
  uint8_t rx[32] = {0}, tx[32] = {0};
  spi_transaction_t t = {.rxlength=256, .tx_buffer=tx, .rx_buffer=rx};
  spi_trans_priv_t p = {.trans=&t, .buffer_to_send=malloc(32), .buffer_to_rcv=NULL};
  uninstall_priv_desc(&p);
  p.buffer_to_send = tx;
  p.buffer_to_rcv = malloc(32);
  memset(p.buffer_to_rcv, 0xa5, 32);
  uninstall_priv_desc(&p);
  assert(rx[0] == 0xa5 && rx[31] == 0xa5);
  p.buffer_to_rcv = rx;
  uninstall_priv_desc(&p);
  p.buffer_to_send = NULL; p.buffer_to_rcv = NULL;
  uninstall_priv_desc(&p);
}
"""
with tempfile.TemporaryDirectory(prefix="spi-oom-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(fixture)
    subprocess.run(["cc", "-fsanitize=address,undefined", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("SPI RX allocation failure and successful buffer cleanup passed")
