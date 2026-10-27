#!/usr/bin/env python3
"""RF CLI payload/response tests; no serial port or companion connection."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import types
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


class FakeLink:
    def __init__(self, status=0, response=b'\x00'):
        self.status, self.response, self.calls = status, response, []

    def cmd(self, category, operation, payload=b''):
        self.calls.append((category, operation, payload))
        return self.status, self.response


def main():
    serial = types.ModuleType('serial')
    tools = types.ModuleType('serial.tools')
    tools.list_ports = types.SimpleNamespace(comports=lambda: [])
    with patch.dict(sys.modules, {'serial': serial, 'serial.tools': tools}):
        spec = importlib.util.spec_from_file_location('host_link_rf_cli_test', ROOT / 'tools/host_link/cli.py')
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        link = FakeLink()
        cli.nfc_read(link, [])
        assert link.calls == [(0x12, 1, b'\x00')]
        for tech, code in cli.NFC_TECHNOLOGIES.items():
            data = bytearray(17)
            data[0], data[1], data[2], data[3] = 1, code, 1, 4
            data[4:8] = b'ABCD'
            link = FakeLink(response=bytes(data))
            cli.nfc_read(link, [tech])
            assert link.calls == [(0x12, 1, bytes([code]))]
        link = FakeLink(response=b'\x01ABCDE\x01\x34\x12')
        cli.rfid_read(link, [])
        assert link.calls == [(0x13, 1, struct.pack('<H', 500))]
        for category in (0x12, 0x13):
            link = FakeLink(response=b'\x01\x02')
            cli.rf_status(link, category)
            assert link.calls == [(category, 3, b'')]
        for func, arguments in ((cli.nfc_read, ['x']), (cli.nfc_read, ['a', 'b']),
                                (cli.rfid_read, ['39']), (cli.rfid_read, ['501'])):
            link = FakeLink()
            try:
                func(link, arguments)
            except ValueError:
                pass
            else:
                raise AssertionError('invalid argument accepted')
            assert not link.calls
        for function in (cli.nfc_read, cli.rfid_read):
            for data in (b'', b'\x01', b'\x00\x00'):
                try:
                    function(FakeLink(response=data), [])
                except ValueError:
                    pass
                else:
                    raise AssertionError('malformed response accepted')
            function(FakeLink(status=1, response=b''), [])
    with contextlib.redirect_stdout(output):
        key='FFFFFFFFFFFF';data='CA'*16
        link=FakeLink(response=bytes.fromhex(data));cli.nfc_block(link,['4',key])
        assert link.calls==[(0x12,4,b'\x04'+bytes.fromhex(key))]
        link=FakeLink(response=b'');cli.nfc_block(link,['4',key,data],True)
        assert link.calls==[(0x12,5,b'\x04'+bytes.fromhex(key+data))]
        for args,is_write in [(['64',key],False),(['4','FF'],False),(['3',key,data],True),(['0',key,data],True),(['4',key,'CA'],True)]:
            link=FakeLink()
            try: cli.nfc_block(link,args,is_write)
            except ValueError: pass
            else: raise AssertionError('invalid block request accepted')
            assert not link.calls
        try: cli.nfc_block(FakeLink(response=b''),['4',key])
        except ValueError: pass
        else: raise AssertionError('short block accepted')
    assert 'no card found' in output.getvalue()
    assert 'samples=4660' in output.getvalue()
    print('PASS: NFC/RFID CLI encodes bounded requests and rejects malformed results without device access')


if __name__ == '__main__':
    main()
