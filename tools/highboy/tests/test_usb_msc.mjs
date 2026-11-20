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

import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

test('USB MSC preserves card ownership through lifecycle failures', () => {
  const temporary = mkdtempSync(path.join(tmpdir(), 'highboy-msc-test-'));
  try {
    // Hardware interfaces are supplied by usb_msc_test.c; use the real public
    // usb_msc.h and include the production implementation in that translation unit.
    for (const header of [
      'esp_log.h', 'esp_system.h', 'freertos/FreeRTOS.h', 'freertos/task.h',
      'host_link.h', 'device/usbd_pvt.h', 'sdmmc_cmd.h', 'tinyusb_msc.h', 'tusb.h', 'tusb_desc.h', 'vfs_sdcard.h',
    ]) {
      const target = path.join(temporary, header);
      mkdirSync(path.dirname(target), { recursive: true });
      const notice = readFileSync(path.join(root, 'tools/highboy/tests/usb_msc_test.c'), 'utf8').split('#include')[0];
      writeFileSync(target, notice + '/* Interface mocked by the test harness. */\n');
    }
    const executable = path.join(temporary, 'usb_msc_test');
    const compiled = spawnSync(process.env.CC || 'cc', [
      '-std=c11', '-Wall', '-Wextra', '-Werror',
      '-I', temporary,
      '-I', path.join(root, 'firmware_p4/components/Service/storage_vfs/include'),
      path.join(root, 'tools/highboy/tests/usb_msc_test.c'), '-o', executable,
    ], { encoding: 'utf8' });
    assert.equal(compiled.status, 0, compiled.error?.message || compiled.stderr);
    const result = spawnSync(executable, [], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.error?.message || result.stderr);
    assert.match(result.stdout, /PASS: MSC lifecycle/);
    console.log(result.stdout.trim());
  } finally {
    rmSync(temporary, { recursive: true, force: true });
  }
});
