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

"""Exercise production SPI transport code with an explicit in-memory PHY/clock."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    master = (ROOT / "firmware_p4/components/Service/spi_bridge/spi_bridge.c").read_text()
    slave = (ROOT / "firmware_c5/components/Service/spi_bridge/spi_bridge.c").read_text()
    helpers = master[master.index("// All transport state"):master.index("typedef struct {\n  spi_id_t id;")]
    classifier = slave[slave.index("typedef enum {\n  SPI_TRANSPORT_DROP"):slave.index("static void bridge_task(void *pvParameters) {")]
    fixture = (HERE / "test_transport.c").read_text()
    stream = slave[slave.index("            uint8_t *recs = tx_buf"):slave.index("          } else if (cmd == SPI_ID_SYSTEM_CFG_HASH)")]
    stream = "static void produce_stream(uint8_t *tx_buf) {\n size_t tx_size=0; bool tx_ready=false;\n" + stream + "\n assert(tx_ready && tx_size==SPI_STREAM_FRAME_SIZE);\n}\n"
    diag = master[master.index("typedef struct {\n  uint32_t serial"):master.index("typedef struct {\n  unsigned headers")]
    receiver = master[master.rindex("static esp_err_t recv_frame(uint8_t *tx_buf,"):master.index("void spi_bridge_register_stream_cb(")]
    glue = "static uint32_t s_rx_serial,s_rx_success;\nstatic void log_rx_failure(const rx_diag_t *d,uint16_t cmd,esp_err_t error){(void)cmd;(void)error;last_wrong_id=d->wrong_id;}\n"
    fixture = fixture.replace("/* PRODUCTION */", classifier + helpers + stream + diag + glue + receiver)
    with tempfile.TemporaryDirectory(prefix="spi-transport-") as tmp:
        source = Path(tmp) / "test.c"
        binary = Path(tmp) / "test"
        source.write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "tools/mqtt/fixtures"),
                        "-I", str(ROOT / "firmware_p4/components/Service/spi_bridge/include"),
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
