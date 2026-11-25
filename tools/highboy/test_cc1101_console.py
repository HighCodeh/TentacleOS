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

"""Check the console validator against independent P1 timings and the real encoder."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'firmware_p4/components/Applications/SubGhz'
text = (ROOT / 'firmware_p4/components/Service/console/commands/cmd_rf.c').read_text()
start = text.index('static bool cc1101_validate_p1_frame(')
end = text.index('static uint8_t cc1101_status_reg', start)
defines = '\n'.join(line for line in text.splitlines() if line.startswith('#define CC1101_TX_DRY_'))
harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "protocol_rcswitch.c"
/* DEFINES */
/* VALIDATOR */
int main(void) {
  const uint32_t code = 0x5AA55A;
  int32_t expected[52] = {350, -10850};
  for (int bit = 23; bit >= 0; --bit) {
    bool one = (code >> bit) & 1;
    expected[2 + (23 - bit) * 2] = one ? 1050 : 350;
    expected[3 + (23 - bit) * 2] = one ? -350 : -1050;
  }
  expected[50] = 350;
  expected[51] = -10850;
  uint32_t total = 0, high = 0;
  assert(cc1101_validate_p1_frame(expected, 52, code, &total, &high));
  assert(total == 56000 && high == 17500);
  assert(!cc1101_validate_p1_frame(expected, 49, code, NULL, NULL));
  assert(!cc1101_validate_p1_frame(expected, 52, code ^ 1, NULL, NULL));
  subghz_data_t data = {.protocol_name="RCSwitch(P1)", .bit_count=24, .raw_value=code};
  int32_t actual[52];
  assert(protocol_rcswitch.encode(&data, actual, 52) == 52);
  assert(memcmp(actual, expected, sizeof(actual)) == 0);
  actual[51] = -350;
  assert(!cc1101_validate_p1_frame(actual, 52, code, NULL, NULL));
  actual[1] = -350;
  assert(!cc1101_validate_p1_frame(actual, 52, code, NULL, NULL));
  puts("PASS: console P1 framing, known code, timing totals and corrupt/truncated rejection");
}
"""
harness = harness.replace('/* DEFINES */', defines).replace('/* VALIDATOR */', text[start:end])
with tempfile.TemporaryDirectory(prefix='highboy-p1-') as temp:
    source = Path(temp) / 'test.c'
    source.write_text(harness)
    binary = Path(temp) / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-fno-sanitize-recover=all', '-I', str(SOURCE/'protocols'),
                    '-I', str(SOURCE/'protocols/include'), '-I', str(SOURCE/'include'),
                    '-I', str(ROOT/'tools/highboy/test_subghz_stubs'), str(source),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
