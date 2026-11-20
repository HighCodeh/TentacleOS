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

"""Exercise the production NFC screen teardown dispatcher with device fakes."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from test_rf_ownership import function

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / 'firmware_p4/components/Applications/ui'


def main():
    source = (UI / 'ui_manager.c').read_text()
    dispatcher = function(source, 'static ui_close_fn_t screen_close_fn(screen_id_t s)')
    screens = list(dict.fromkeys(re.findall(r'case (SCREEN_\w+):', dispatcher)))
    callbacks = set(re.findall(r'return (\w+);', dispatcher)) - {'NULL'}
    expected = {
        'SCREEN_SCRIPTS': 'ui_scripts_stop',
        'SCREEN_NFC_READ': 'ui_nfc_read_stop',
        'SCREEN_NFC_SAVED': 'ui_nfc_saved_stop',
        'SCREEN_NFC_WRITE': 'ui_nfc_write_stop',
        'SCREEN_NFC_EMULATE': 'ui_nfc_emulate_stop',
        'SCREEN_CARD_EMU': 'ui_nfc_emulate_stop',
        'SCREEN_NFC_SCAN': 'ui_nfc_scan_stop',
        'SCREEN_NFC_BANKCARD': 'ui_nfc_bankcard_stop',
    }
    declarations = '\n'.join('static void ' + name + '(void) {}' for name in sorted(callbacks))
    assertions = '\n'.join(
        f'assert(screen_close_fn({screen}) == {callback});' for screen, callback in expected.items())
    notice = (UI / 'screens/nfc/card_emu_ui.c').read_text().split('#include', 1)[0]
    harness = notice + '''#include <assert.h>
#include <stddef.h>
#include "card_emu_ui.h"
typedef void (*ui_close_fn_t)(void);
static unsigned native_opens;
void ui_nfc_emulate_open(void) { ++native_opens; }
'''
    harness += 'typedef enum {' + ','.join(screens) + '} screen_id_t;\n'
    harness += declarations + '\n' + dispatcher + '\n'
    harness += 'int main(void) {\n' + assertions
    harness += '''
assert(screen_close_fn((screen_id_t)-1) == NULL);
ui_card_emu_open();
assert(native_opens == 1);
return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='highboy-nfc-screen-') as temporary:
        directory = Path(temporary)
        (directory / 'esp_log.h').write_text(notice + '#define ESP_LOGD(tag, ...) ((void)(tag))\n')
        (directory / 'esp_err.h').write_text(notice + 'typedef int esp_err_t;\n')
        (directory / 'ui_manager.h').write_text(notice + 'typedef int screen_id_t;\n')
        (directory / 'test.c').write_text(harness)
        executable = directory / 'test'
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', str(directory), '-I', str(UI / 'screens/nfc/include'),
                        str(directory / 'test.c'), str(UI / 'screens/nfc/card_emu_ui.c'),
                        '-o', str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
    print('PASS: native NFC legacy entry and read/write/emulate/scan/bankcard teardown routing')


if __name__ == '__main__':
    main()
