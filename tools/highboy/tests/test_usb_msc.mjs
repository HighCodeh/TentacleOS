import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
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
      'sdmmc_cmd.h', 'tinyusb_msc.h', 'tusb.h', 'tusb_desc.h', 'vfs_sdcard.h',
    ]) {
      const target = path.join(temporary, header);
      mkdirSync(path.dirname(target), { recursive: true });
      writeFileSync(target, '/* Interface mocked by the test harness. */\n');
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
    assert.match(result.stdout, /PASS: 8 MSC lifecycle groups/);
    console.log(result.stdout.trim());
  } finally {
    rmSync(temporary, { recursive: true, force: true });
  }
});
