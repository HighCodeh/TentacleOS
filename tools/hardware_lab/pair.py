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

"""Repeatable LF RFID comparison using a Flipper Zero as the reference."""
import argparse
import json
from pathlib import Path
import signal
import threading
import time

import lab

TEST_ID = '0102030405'

class Flipper:
    def __init__(self, port, output):
        import serial
        found = [p for p in lab.ports() if p['device'] == port]
        if not found or (found[0]['vid'], found[0]['pid']) != (0x0483, 0x5740):
            raise RuntimeError('Select a connected Flipper Zero CDC port explicitly.')
        self.output = output
        self.events = []
        self.active = False
        self.u = serial.Serial(port=None, baudrate=115200, timeout=.1,
                               write_timeout=1, exclusive=True)
        # Flipper CDC requires DTR to announce a CLI session. This is not an ESP reset.
        self.u.dtr = True
        self.u.rts = False
        self.u.port = port
        self.u.open()
        try:
            self.receive(1.2)
            self.send(b'\x03\r\n')
            self.receive(.5)
            self.send(b'device_info\r\n')
            if 'hardware_model' not in self.receive(1):
                raise RuntimeError('Flipper CLI not responding; close qFlipper first.')
        except BaseException:
            self.u.close()
            raise

    def record(self, kind, value):
        row = dict(utc=lab.dt.datetime.now(lab.dt.timezone.utc).isoformat(), kind=kind, value=value)
        self.events.append(row)
        lab.save(self.output, self.events)
        print('\nFLIPPER ' + kind.upper() + ': ' + value, flush=True)

    def send(self, data):
        self.record('tx', repr(data))
        self.u.write(data)

    def receive(self, seconds):
        end = time.monotonic() + seconds
        data = bytearray()
        while time.monotonic() < end:
            data.extend(self.u.read(min(8192, max(1, self.u.in_waiting))))
            if len(data) > lab.MAX_CAPTURE:
                raise RuntimeError('Flipper capture limit exceeded.')
        value = lab.clean(data)
        self.record('rx', value)
        return value

    def start(self, reader=False):
        self.active = True
        command = 'rfid read normal' if reader else 'rfid emulate EM4100 ' + TEST_ID
        self.send((command + '\r\n').encode())
        reply = self.receive(1)
        marker = 'Reading RFID' if reader else 'Emulating RFID'
        if marker not in reply:
            raise RuntimeError('Flipper did not start: ' + reply)

    def stop(self):
        if self.active:
            self.send(b'\x03')
            reply = self.receive(1.5)
            if 'stopped' not in reply or '>: ' not in reply:
                raise RuntimeError('Flipper stop not confirmed; inspect its screen.')
            self.active = False

    def close(self):
        try:
            self.stop()
        finally:
            self.u.close()


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--highboy', required=True)
    p.add_argument('--flipper', required=True)
    p.add_argument('--label', required=True, help='Board serial + capacitor revision')
    p.add_argument('--notes', default='')
    p.add_argument('--mode', choices=('read', 'load', 'deep'), default='read')
    p.add_argument('--output', type=Path, default=lab.OUTPUT)
    args = p.parse_args(argv)
    stamp = lab.dt.datetime.now(lab.dt.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output = args.output / ('pair-' + stamp + '-' + lab.uuid.uuid4().hex[:6])
    output.mkdir(parents=True)
    cancelled = threading.Event()
    runner = lab.Runner(output, lab.terminal_event)
    def stop(*_):
        cancelled.set()
        runner.cancelled.set()
        print('\nSTOP requested: finishing current bounded command, then checking OFF.', flush=True)
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    report = dict(label=args.label, notes=args.notes, test_id=TEST_ID, status='running',
                  highboy=args.highboy, flipper=args.flipper, runs=[],
                  tag_emulation_acceptance='not_tested: read/load/deep modes do not run rfid emutest')
    flipper = None
    try:
        flipper = Flipper(args.flipper, output / 'flipper.json')
        modes = ('read', 'load') if args.mode == 'deep' else (args.mode,)
        for mode in modes:
            if cancelled.is_set():
                raise lab.Cancelled('Stopped between cases.')
            flipper.start(reader=mode == 'load')
            cases = ('lf-selftest', 'lf-sweep', 'lf-field', 'lf-raw', 'lf-read') if mode == 'read' else ('lf-passive', 'lf-modulation')
            for case in cases:
                if cancelled.is_set():
                    raise lab.Cancelled('Stopped between cases.')
                print('\n>>> HIGH BOY TEST: ' + case, flush=True)
                result = runner.run(case, args.highboy, args.label, args.notes,
                                    cycles=3 if case in ('lf-field', 'lf-read', 'lf-passive') else 1)
                report['runs'].append({k: result.get(k) for k in ('case', 'status', 'rows', 'error', 'output')})
                lab.save(output / 'comparison.json', report)
                if result['status'] != 'complete':
                    raise RuntimeError('High Boy test stopped: ' + str(result.get('error')))
            flipper.stop()
        report['status'] = 'complete'
    except lab.Cancelled as exc:
        report.update(status='cancelled', error=str(exc))
    except Exception as exc:
        report.update(status='failed', error=str(exc))
    finally:
        if flipper:
            try:
                flipper.close()
                report['flipper_stop_confirmed'] = True
            except Exception as exc:
                report.update(status='failed', stop_error=str(exc))
        lab.save(output / 'comparison.json', report)
        print('\nSESSION ' + report['status'].upper() + ': ' + str(output), flush=True)
        for run in report['runs']:
            print(run['case'], json.dumps(run['rows'], ensure_ascii=False), flush=True)
        print('EM4100 emulation acceptance on High Boy: NOT TESTED; load pulses are not tag frames.', flush=True)
    return 0 if report['status'] == 'complete' else 1

if __name__ == '__main__':
    raise SystemExit(main())
