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

"""Verify the production SPI admission functions under display/peripheral contention."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / "firmware_p4/components/Drivers/spi/spi.c").read_text()
functions = source[source.index("bool spi_bus_lock_take("):source.index("void spi_bus_lock_give(")]
fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static void *s_spi3_lock = (void *)1;
static atomic_uint s_spi3_waiters;
static TickType_t ticks, passed_timeout;
static unsigned takes, delays;
static bool wake_peripheral, fail_take, is_peripheral;
static TickType_t xTaskGetTickCount(void) { return ticks; }
static void vTaskDelay(TickType_t delay) {
  ticks += delay;
  delays++;
  if (wake_peripheral) atomic_store(&s_spi3_waiters, 0);
}
static int xSemaphoreTake(void *lock, TickType_t timeout) {
  assert(lock);
  assert(atomic_load(&s_spi3_waiters) == (is_peripheral ? 1U : 0U));
  takes++;
  passed_timeout = timeout;
  return !fail_take;
}
"""
checks = r"""
int main(void) {
  assert(spi_bus_lock_take_display(50));
  assert(takes == 1 && !delays && passed_timeout == 50);
  atomic_store(&s_spi3_waiters, 1);
  wake_peripheral = true;
  assert(spi_bus_lock_take_display(50));
  assert(delays == 1 && takes == 2 && passed_timeout == 49);
  wake_peripheral = false;
  atomic_store(&s_spi3_waiters, 1);
  assert(!spi_bus_lock_take_display(3));
  assert(takes == 2 && delays == 4);
  atomic_store(&s_spi3_waiters, 0);
  is_peripheral = true;
  assert(spi_bus_lock_take(1000));
  assert(!atomic_load(&s_spi3_waiters));
  fail_take = true;
  assert(!spi_bus_lock_take(1000));
  assert(!atomic_load(&s_spi3_waiters));
  is_peripheral = false;
  ticks = UINT32_MAX;
  atomic_store(&s_spi3_waiters, 1);
  assert(!spi_bus_lock_take_display(3));
  s_spi3_lock = 0;
  assert(spi_bus_lock_take(0) && spi_bus_lock_take_display(0));
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-spi-fairness-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(fixture + functions + checks)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined,address", str(path / "test.c"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)
print("PASS: peripheral admission, display yield, bounded timeout and tick wrap")
