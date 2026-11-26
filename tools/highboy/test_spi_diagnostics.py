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

"""Exercise retained production SPI diagnostics without a device."""
import subprocess
import tempfile
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "firmware_p4/components/Service/spi_bridge/spi_bridge.c").read_text()
header = (ROOT / "firmware_p4/components/Service/spi_bridge/include/spi_bridge.h").read_text()
start = header.index("typedef enum {\n  SPI_BRIDGE_PHASE_LOCAL")
end = header.index("/** Read retained", start)
types = header[start:end]
def function(name):
    match = re.search(r"(?:static\s+)?esp_err_t\s+" + name + r"\([^;{]*\{.*?\n}", source, re.S)
    assert match, name
    return match.group()

production = """static spi_bridge_failure_t s_failures[SPI_BRIDGE_FAILURE_COUNT];
static portMUX_TYPE s_failure_lock = portMUX_INITIALIZER_UNLOCKED;
"""
production += function("retain_failure")
production += function("spi_bridge_get_failures")
fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint16_t spi_id_t;
typedef int esp_err_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define SPI_ID_BOARD_NEGOTIATE 0x0e
#define SPI_ID_BOARD_STATE 0x0f
#define SPI_ID_BOARD_SET 0x10
#define SPI_CAT_ETHERNET 0x15
#define SPI_ID_ETHERNET_STOP 0x1502
#define SPI_CMD_CAT(id) ((id)>>8)
static int64_t now;
static int64_t esp_timer_get_time(void){return now;}
"""
tests = r"""
int main(void) {
 spi_bridge_failure_t out[SPI_BRIDGE_FAILURE_COUNT];
 assert(spi_bridge_get_failures(NULL,false)==ESP_ERR_INVALID_ARG);
 now=500;
 assert(retain_failure(0x1505,SPI_BRIDGE_PHASE_LOCK,100,ESP_ERR_TIMEOUT)==ESP_ERR_TIMEOUT);
 now=900;
 retain_failure(0x1504,SPI_BRIDGE_PHASE_RECEIVE,600,ESP_ERR_TIMEOUT);
 retain_failure(SPI_ID_ETHERNET_STOP,SPI_BRIDGE_PHASE_RECEIVE,600,ESP_ERR_TIMEOUT);
 retain_failure(0x1505,SPI_BRIDGE_PHASE_PEER,600,ESP_ERR_INVALID_STATE);
 retain_failure(SPI_ID_BOARD_STATE,SPI_BRIDGE_PHASE_RECEIVE,700,ESP_ERR_TIMEOUT);
 retain_failure(0x1234,SPI_BRIDGE_PHASE_SEND,800,ESP_ERR_TIMEOUT);
 retain_failure(0x1505,SPI_BRIDGE_PHASE_PEER,800,ESP_OK);
 assert(spi_bridge_get_failures(out,false)==ESP_OK);
 assert(out[1].count==2 && out[1].command==0x1505 && out[1].phase==SPI_BRIDGE_PHASE_LOCK);
 assert(out[1].time_us==500 && out[1].elapsed_us==400);
 assert(out[0].count==1 && out[0].phase==SPI_BRIDGE_PHASE_RECEIVE);
 assert(out[2].count==1 && out[2].phase==SPI_BRIDGE_PHASE_SEND);
 assert(spi_bridge_get_failures(out,true)==ESP_OK && out[1].count==2);
 assert(spi_bridge_get_failures(out,false)==ESP_OK);
 for(unsigned i=0;i<SPI_BRIDGE_FAILURE_COUNT;i++)assert(out[i].count==0);
 puts("SPI diagnostic retention, grouping, cleanup exclusion and clear passed");
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-spi-diag-") as tmp:
    path = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    path.write_text(fixture + types + production + tests)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
