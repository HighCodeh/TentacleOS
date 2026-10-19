#!/usr/bin/env python3
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
        'SCREEN_NFC_WRITE': 'ui_nfc_write_stop',
        'SCREEN_NFC_EMULATE': 'ui_nfc_emulate_stop',
        'SCREEN_CARD_EMU': 'ui_nfc_emulate_stop',
        'SCREEN_NFC_SCAN': 'ui_nfc_scan_stop',
        'SCREEN_NFC_BANKCARD': 'ui_nfc_bankcard_stop',
    }
    declarations = '\n'.join('static void ' + name + '(void) {}' for name in sorted(callbacks))
    assertions = '\n'.join(
        f'assert(screen_close_fn({screen}) == {callback});' for screen, callback in expected.items())
    harness = '''#include <assert.h>
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
        (directory / 'esp_log.h').write_text('#define ESP_LOGD(tag, ...) ((void)(tag))\n')
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
