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

"""High Boy Lab: bounded tests, explicit triggers and timestamped evidence."""

import argparse
import base64
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import struct
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'integration' / 'hardware_lab'
NOTICE = '\n'.join(line[2:] if line.startswith('# ') else ''
                   for line in Path(__file__).read_text().splitlines()[:15])
ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')
PROMPT = re.compile(r'(?:^|[\r\n])highboy> *$')
FATAL = re.compile(r'Guru Meditation|Brownout detector|assert failed|ESP-ROM:')
MAX_CAPTURE = 2 * 1024 * 1024
CP2105 = (0x10C4, 0xEA70)


def case(title, group, command, duration, probes, expected):
    return dict(title=title, group=group, command=command, timeout=duration,
                probes=probes, expected=expected)


CASES = {
    'baseline': case('Board baseline / Estado inicial', 'System', None, 10,
                     'J24 3.3 V, J25 VBAT, J26 USB 5 V, J27 VSYS; J23 GND.',
                     'Firmware identity, power telemetry and LF idle state; no RF start.'),
    'lf-field': case('125 kHz carrier / Campo', 'RFID', 'rfid field 1000', 5,
                     'GPIO2, T4/T3/T5 drive; U12 CL. Differential probe across coil.',
                     'PCNT carrier near 125 kHz; measure coil amplitude/current separately.'),
    'lf-selftest': case('LF self-test / Autoteste', 'RFID', 'rfid selftest', 5,
                        'GPIO2, GPIO11, CL. External readers must be OFF.',
                        'Static GPIO, carrier feedback and frequency checked separately.'),
    'lf-raw': case('Receive edges / Sinais DA', 'RFID', 'rfid raw 250', 5,
                   'Known EM4100 tag: U11 pin 1 -> U14 pin 1 -> U14 pin 7 -> GPIO16 (DA).',
                   'Transition intervals at DA. Edges alone do not prove a decoded tag.'),
    'lf-read': case('Read known tag / Ler tag', 'RFID', 'rfid watch 5', 9,
                    'Same known EM4100/EM4102 tag, position and spacing for every capacitor variant.',
                    'At least one parity-checked EM4100 frame within 5 s; capture ID and count.'),
    'lf-sweep': case('118–132 kHz feedback / Varredura', 'RFID',
                     'rfid sweep 118000 132000 1000 100', 7,
                     'Trigger scope on drive. Keep tag/coil position fixed.',
                     'Digital feedback by frequency. Resonance requires measured analog amplitude.'),
    'lf-passive': case('External reader field / Campo externo', 'RFID',
                       'rfid listen 1000', 5,
                       'External LF reader ON; High Boy transmitter stays OFF. Probe CL.',
                       'External carrier measured; no carrier is reported as NOT DETECTED.'),
    'lf-modulation': case('T2 load modulation / Modulação', 'RFID',
                          'rfid modtest 1000 1000', 5,
                          'External 125 kHz reader ON. Probe GPIO11/T2 and reader envelope.',
                          '1 kHz load pulse for 1 s. This is NOT protocol-valid tag emulation.'),
    'spi-link': case('P4 ↔ C5 link', 'System', 'c5 linktest 20', 20,
                     '3.3 V and C5 EN; SPI CS/CLK/MISO/MOSI if a request fails.',
                     '60 read-only requests, zero failures. Does not measure radio performance.'),
    'power': case('Battery / Alimentação', 'System', 'battery', 5,
                  'J24–J27, R15 battery current sense with appropriate differential probe.',
                  'Telemetry snapshot only; gauge SoC/temperature may be unqualified.'),
    'nfc': case('NFC field / Campo 13.56 MHz', 'NFC', 'nfc measure 32', 7,
                'ST25R3916B supply and differential coil signal; no LF reader nearby.',
                'Chip response, raw amplitude and field-off confirmation; no card write.'),
    'ir-rx': case('IR reception / Recepção IR', 'Infrared', 'ir rx 5000', 8,
                  'U16 supply/output and C5 GPIO10; send a known 38 kHz remote code.',
                  'Decoded protocol/address/command, or explicit timeout.'),
    'cc-rx': case('Sub-GHz receive / Recepção', 'Radio', None, 12,
                  'CC1101 / T1 / C137 / J2. Use a characterized RF source.',
                  '433.92 MHz RX for 2 s, path 1. Control completion is not sensitivity.'),
    'sx-rx': case('LoRa receive / Recepção', 'Radio', None, 12,
                  'SX1262 BUSY, supply, U19 and antenna. Matching reference transmitter.',
                  '868.1 MHz SF7 BW125 CR4/5 RX for 2 s. Record received test packets.'),
    'usb-reset': case('USB automatic reset / Reset automático', 'USB', None, 15,
                      'CP2105 RTS -> reset stage -> EN; 3.3 V. Trigger on EN falling.',
                      'Fresh ROM log for selected chip, flash boot, then application startup.'),
    'usb-boot': case('USB automatic boot / Boot automático', 'USB', None, 20,
                     'CP2105 DTR/RTS, EN, P4 GPIO35/36 or C5 GPIO28/27.',
                     'Fresh download-mode ROM log + SYNC ACK, followed by reset to flash boot.'),
}


def ports():
    from serial.tools import list_ports
    return [{k: getattr(p, k, None) for k in
             ('device', 'vid', 'pid', 'serial_number', 'interface', 'location', 'description')}
            for p in list_ports.comports()]


def clean(data):
    return ANSI.sub('', data.decode('utf-8', 'replace') if isinstance(data, (bytes, bytearray)) else data)


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    content = value if isinstance(value, str) else json.dumps(value, indent=2, ensure_ascii=False) + '\n'
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(content)
    temporary.replace(path)
    path.with_name(path.name + '.license').write_text(NOTICE + '\n')


class Cancelled(Exception):
    pass


class Link:
    """Single owner. Never choose a port by its suffix; require CP2105 identity."""

    def __init__(self, port, event):
        import serial
        candidates = [p for p in ports() if p['device'] == port]
        if not candidates or (candidates[0]['vid'], candidates[0]['pid']) != CP2105:
            raise RuntimeError('Select a connected High Boy CP2105 port explicitly.')
        self.info = candidates[0]
        self.event = event
        self.u = serial.Serial(port=None, baudrate=115200, timeout=.05,
                               write_timeout=1, exclusive=True)
        self.u.dtr = False
        self.u.rts = False
        self.u.port = port
        try:
            self.u.open()
            self.event('port_open', self.info)
            self.read(.4, allow_reset=True)
        except BaseException:
            self.u.close()
            raise

    def read(self, seconds, allow_reset=False):
        deadline = time.monotonic() + seconds
        data = bytearray()
        while time.monotonic() < deadline:
            part = self.u.read(min(8192, max(1, self.u.in_waiting)))
            if part:
                self.event('rx', dict(text=clean(part), base64=base64.b64encode(part).decode()))
                data.extend(part)
                if len(data) > MAX_CAPTURE:
                    raise RuntimeError('Serial capture limit exceeded.')
        if not allow_reset and FATAL.search(clean(data)):
            raise RuntimeError('Device restarted or reported a fatal fault; inspect the raw log.')
        return bytes(data)

    def command(self, command, timeout=5):
        self.read(.1)
        self.event('command', command)
        # CRLF also completes input in the ESP-IDF console's dumb/fgets mode.
        self.u.write((command + '\r\n').encode())
        deadline = time.monotonic() + timeout
        data = bytearray()
        while time.monotonic() < deadline:
            data.extend(self.read(.1))
            if len(data) > MAX_CAPTURE:
                raise RuntimeError('Command capture limit exceeded.')
            text = clean(data)
            if FATAL.search(text):
                raise RuntimeError('Device restarted or reported a fatal fault during the command.')
            # A redraw while the command is being typed is not its completion.
            if '\n' in text and PROMPT.search(text):
                return text
        raise RuntimeError('No completed console response: ' + (command or '<prompt>'))

    def console(self):
        self.command('\x15', 5)

    def lines(self, dtr, rts):
        self.u.dtr = dtr
        self.u.rts = rts
        self.event('control_lines', dict(dtr_asserted=dtr, rts_asserted=rts,
                                        timing='host request; verify physical edges on scope'))

    def close(self):
        try:
            self.lines(False, False)
        finally:
            self.u.close()


def idle(text):
    return (re.search(r'RF frontend: idle\s', text) is not None and
            re.search(r'LF: field=OFF .*EN\(GPIO2\)=0 MOD\(GPIO11\)=0', text) is not None)


def field(text):
    match = re.search(r'LF field: (LOCKED|NO CARRIER FEEDBACK), source=(.*?) '
                      r'reference=(\d+) Hz generated=(\d+) Hz measured=(\d+) Hz', text)
    if not match:
        raise RuntimeError('Missing LF measurement; unsupported firmware or failed command.')
    counts = re.search(r'CL rises=(\d+); DA edges=(\d+) \((\d+) edges/s\)', text)
    return dict(carrier_detected=match[1] == 'LOCKED', source=match[2],
                requested_hz=int(match[3]), generated_hz=int(match[4]), measured_hz=int(match[5]),
                cl_rises=int(counts[1]) if counts else None,
                da_edges=int(counts[2]) if counts else None)


def inspect(case_id, text):
    if case_id == 'lf-selftest':
        match = re.search(r'LF self-test: GPIO static=(PASS|FAIL) carrier=(PASS|FAIL) '
                          r'frequency=(PASS|FAIL) result=(PASS|FAIL)', text)
        if not match:
            raise RuntimeError('Missing LF self-test result.')
        return dict(result='observed' if match[4] == 'PASS' else 'not_detected',
                    gpio_static=match[1], carrier=match[2], firmware_frequency_check=match[3],
                    frequency_measured=match[2] == 'PASS', **field(text))
    if case_id in ('lf-field', 'lf-passive', 'lf-modulation'):
        if case_id == 'lf-modulation' and 'ESP_ERR_NOT_FOUND' in text:
            return dict(result='not_detected', detail='External LF field missing; modulation refused.')
        values = field(text)
        if case_id == 'lf-modulation' and 'T2 load-path test: PASS command path' not in text:
            raise RuntimeError('T2 command did not complete.')
        return dict(result='observed' if values['carrier_detected'] else 'not_detected', **values)
    if case_id == 'lf-read':
        match = re.search(r'RFID live reader finished: attempts=(\d+) valid=(\d+)', text)
        if not match:
            raise RuntimeError('Missing RFID reader completion.')
        tags = sorted(set(re.findall(r'EM4100(?: LIVE)?:?\s+(?:ID=)?([0-9A-Fa-f]{10})', text)))
        if int(match[2]) and not tags:
            raise RuntimeError('Reader reported valid frames without a captured tag ID.')
        return dict(result='observed' if int(match[2]) else 'not_detected',
                    attempts=int(match[1]), valid_frames=int(match[2]),
                    tags=tags)
    if case_id == 'lf-raw':
        if 'raw capture failed: ESP_ERR_NOT_FOUND' in text:
            return dict(result='not_detected', da_edges=0)
        match = re.search(r'DA edges captured=(\d+)', text)
        if not match:
            raise RuntimeError('Missing raw edge capture.')
        pairs = re.findall(r'\b(\d+),([01])\b', text[match.end():])
        if len(pairs) != int(match[1]):
            raise RuntimeError('Truncated raw edge capture.')
        return dict(result='observed', da_edges=int(match[1]),
                    edges=[dict(delta_us=int(a), level=int(b)) for a, b in pairs])
    if case_id == 'lf-sweep':
        rows = re.findall(r'(?m)^\s*(\d+),(\d+),(\d+),(-?\d+),(\d+),(\d+)\s*$', text)
        if len(rows) != 15 or [int(r[0]) for r in rows] != list(range(118000, 133000, 1000)):
            raise RuntimeError('Incomplete LF sweep; inspect firmware response.')
        return dict(result='observed', rows=[list(map(int, r)) for r in rows])
    if case_id == 'spi-link':
        if not re.search(r'C5 LINK PASS: ok=60 failed=0 attempted=60/60', text):
            raise RuntimeError('SPI link qualification failed or incomplete.')
    elif case_id == 'power':
        if 'BQ25896 silicon' not in text or not re.search(r'VBAT\s+: \d+ mV', text):
            raise RuntimeError('Missing battery telemetry.')
    elif case_id == 'nfc':
        if 'NFC RF measurement: chip_id=0x31' not in text:
            raise RuntimeError('NFC field measurement failed.')
    elif case_id == 'ir-rx':
        if 'ir: no frame (timeout)' in text:
            return dict(result='not_detected', detail='No IR frame received.')
        if 'ir: frame received but not decoded' in text:
            return dict(result='not_decoded', detail='IR frame received, protocol unknown.')
        if not re.search(r'ir: \w+ addr=0x[\dA-Fa-f]+ cmd=0x[\dA-Fa-f]+', text):
            raise RuntimeError('IR receive failed.')
    return dict(result='observed')


def slip(payload):
    return b'\xc0' + payload.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc') + b'\xc0'


def sync_ok(data):
    for packet in data.split(b'\xc0')[1:-1]:
        packet = packet.replace(b'\xdb\xdc', b'\xc0').replace(b'\xdb\xdd', b'\xdb')
        if len(packet) == 12 and packet[:4] == b'\x01\x08\x04\x00' and packet[-4:-2] == b'\x00\x00':
            return True
    return False


def boot_observation(data, chip):
    text = clean(data)
    return dict(chip_confirmed=bool(re.search(r'ESP-ROM:esp32' + chip + r'[-\s]', text, re.I)),
                download=bool(re.search(r'boot:.*DOWNLOAD|waiting for download', text, re.I)),
                flash_boot='SPI_FAST_FLASH_BOOT' in text,
                application_started=('highboy>' in text or 'Console started.' in text or
                                     'Returned from app_main()' in text))


def reset(link, download=False):
    link.read(.2, allow_reset=True)
    link.lines(False, True)
    time.sleep(.1)
    if download:
        link.lines(True, False)
        time.sleep(.05)
        link.lines(False, False)
    else:
        link.lines(False, False)
    return link.read(3 if download else 8, allow_reset=True)


def usb_test(link, chip, download, cancelled):
    result = {}
    try:
        data = reset(link, download)
        result['boot'] = boot_observation(data, chip)
        if cancelled.is_set():
            raise Cancelled('Stopped; restoring released control lines.')
        if download and result['boot']['chip_confirmed'] and result['boot']['download']:
            link.read(.2, allow_reset=True)
            packet = slip(struct.pack('<BBHI', 0, 8, 36, 0) + b'\x07\x07\x12\x20' + b'\x55' * 32)
            link.event('rom_sync', 'Read-only SYNC opcode 0x08; no RAM upload or flash command.')
            link.u.write(packet)
            result['sync_ack'] = sync_ok(link.read(1, allow_reset=True))
    finally:
        # Always try to return to the application, including cancellation in ROM.
        if download:
            result['recovery'] = boot_observation(reset(link), chip)
            link.event('usb_recovery', result['recovery'])
        link.lines(False, False)
    boot = result['boot']
    passed = boot['chip_confirmed'] and (boot['download'] and result.get('sync_ack')
                                        if download else boot['flash_boot'] and boot['application_started'])
    if download:
        recovery = result['recovery']
        passed = passed and recovery['chip_confirmed'] and recovery['flash_boot'] and recovery['application_started']
    result['result'] = 'observed' if passed else 'failed'
    return result


def radio_status(link):
    text = link.command('rfbench status')
    match = re.search(r'STATUS (\{[^\r\n]+\})', text)
    if not match:
        raise RuntimeError('RF bench unavailable; install the lab/rf firmware.')
    return json.loads(match[1])


def radio_test(link, radio, cancelled):
    before = radio_status(link)
    if before['phase'] != 0 or before['cleanup_error']:
        raise RuntimeError('RF bench busy or cleanup retained; resolve it before starting.')
    parameters = 'cc rx 433920000 -30 1' if radio == 'cc' else 'sx rx 868100000 -9 0'
    started = False
    try:
        started = True
        reply = link.command('rfbench ' + parameters + ' 7 125 1 2000 1000 ARMED')
        if 'REQUEST ESP_OK' not in reply:
            raise RuntimeError('Radio request rejected.')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if cancelled.is_set():
                raise Cancelled('Radio test stop requested.')
            state = radio_status(link)
            if state['id'] != before['id'] + 1:
                raise RuntimeError('Radio run ID changed unexpectedly.')
            if state['phase'] in (0, 4):
                return dict(result='observed' if state['phase'] == 0 and not state['error'] and
                            not state['cleanup_error'] else 'failed', final=state)
            time.sleep(.1)
        raise RuntimeError('Radio completion deadline exceeded.')
    finally:
        if started:
            link.command('rfbench stop')
            end = time.monotonic() + 5
            while True:
                state = radio_status(link)
                if state['phase'] == 0 and state['cleanup_error'] == 0:
                    break
                if time.monotonic() >= end:
                    raise RuntimeError('Radio OFF not confirmed; inspect retained cleanup.')


class Runner:
    def __init__(self, output=OUTPUT, progress=None):
        self.output = Path(output)
        self.progress = progress or (lambda event: None)
        self.cancelled = threading.Event()
        self.lock = threading.Lock()
        self.report = None

    def run(self, case_id, port, label='', notes='', chip='p4', cycles=1):
        if case_id not in CASES or chip not in ('p4', 'c5') or type(cycles) is not int or not 1 <= cycles <= 10:
            raise ValueError('Invalid case, chip or cycle count (1..10).')
        if not case_id.startswith('usb-') and chip != 'p4':
            raise ValueError('Console presets require the P4 port; C5 selection is for USB tests.')
        if len(label) > 120 or len(notes) > 4000:
            raise ValueError('Label/notes too long.')
        if not self.lock.acquire(blocking=False):
            raise RuntimeError('Another test is running; stop or wait before starting.')
        self.cancelled.clear()
        test = CASES[case_id]
        start = time.monotonic()
        run_id = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
        folder = self.output / run_id
        self.report = dict(schema=1, id=run_id, utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                           case=case_id, label=label, notes=notes, requested_port=port, chip=chip,
                           scope=test, status='running', rows=[], events=[],
                           script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                           instrument_verification='pending', output=str(folder))
        link = None
        log = None

        def event(kind, value):
            row = dict(elapsed_s=round(time.monotonic() - start, 6), kind=kind, value=value)
            self.report['events'].append(row)
            if log:
                log.write(json.dumps(row, ensure_ascii=False) + '\n')
                log.flush()
            self.progress(row)

        try:
            folder.mkdir(parents=True, exist_ok=False)
            save(folder / 'events.jsonl', '')
            log = (folder / 'events.jsonl').open('a')
            link = Link(port, event)
            self.report['usb'] = link.info
            if not case_id.startswith('usb-'):
                link.console()
                self.report['firmware'] = link.command('rfbench info')
                if not re.search(r'INFO schema=1 version=.*elf_sha256=[0-9a-f]{64}', self.report['firmware']):
                    raise RuntimeError('Expected lab firmware identity missing; no active test started.')
                self.report['battery_before'] = link.command('battery')
            for index in range(cycles):
                if self.cancelled.is_set():
                    raise Cancelled('Stopped between bounded operations.')
                event('cycle', index + 1)
                if case_id.startswith('usb-'):
                    row = usb_test(link, chip, case_id == 'usb-boot', self.cancelled)
                elif case_id in ('cc-rx', 'sx-rx'):
                    row = radio_test(link, case_id[:2], self.cancelled)
                elif case_id == 'baseline':
                    row = dict(result='observed', lf_status=link.command('rfid status'),
                               c5_status=link.command('c5 status'))
                else:
                    row = self.console_test(link, case_id)
                self.report['rows'].append(row)
                event('result', row)
                save(folder / 'report.json', self.report)
                if row['result'] == 'failed':
                    raise RuntimeError('Test did not meet its control criteria; see recorded observations.')
            if self.cancelled.is_set():
                raise Cancelled('Stopped; bounded operation completed and cleanup checked.')
            if not case_id.startswith('usb-'):
                self.report['battery_after'] = link.command('battery')
            self.report['status'] = 'complete'
        except Cancelled as exc:
            self.report.update(status='cancelled', error=str(exc))
        except Exception as exc:
            self.report.update(status='failed', error=str(exc))
        finally:
            if link:
                try:
                    link.close()
                except Exception as exc:
                    self.report.update(status='failed', close_error=str(exc))
            if log:
                log.close()
            try:
                self.report['elapsed_s'] = round(time.monotonic() - start, 3)
                save(folder / 'report.json', self.report)
                lines = ['# High Boy Lab', '', 'Case: ' + case_id, 'Label: ' + label,
                         'Status: ' + self.report['status'], '', 'Notes: ' + notes, '',
                         'Instrument verification: pending. Control completion is not electrical qualification.',
                         '', '```json', json.dumps(self.report['rows'], indent=2), '```']
                if self.report.get('error'):
                    lines.extend(['', 'Error: ' + self.report['error']])
                save(folder / 'summary.md', '<!--\n' + NOTICE + '\n-->\n' + '\n'.join(lines) + '\n')
            finally:
                self.lock.release()
        return self.report

    def console_test(self, link, case_id):
        needs_lf = case_id.startswith('lf-')
        needs_nfc = case_id == 'nfc'
        if needs_lf or needs_nfc:
            if not idle(link.command('rfid status')):
                raise RuntimeError('Shared antenna is not idle; close the NFC/RFID application first.')
            passive = field(link.command('rfid listen 100'))
            if case_id not in ('lf-passive', 'lf-modulation') and passive['carrier_detected']:
                raise RuntimeError('External LF field detected; turn the reference reader off first.')
        attempted = False
        try:
            if self.cancelled.is_set():
                raise Cancelled('Stopped before activation.')
            attempted = True
            text = link.command(CASES[case_id]['command'], CASES[case_id]['timeout'])
            return inspect(case_id, text)
        finally:
            if attempted and needs_lf:
                link.command('rfid off')
                if not idle(link.command('rfid status')):
                    raise RuntimeError('LF OFF not confirmed. Inspect the output and disconnect power before rework.')
            if attempted and needs_nfc:
                off = link.command('nfc field off')
                status = link.command('nfc status')
                if 'NFC field off: ESP_OK' not in off or not re.search(r'NFC: .* field=OFF ', status):
                    raise RuntimeError('NFC OFF not confirmed; cleanup needs attention.')


def terminal_event(event):
    kind, value = event['kind'], event['value']
    stamp = f"[{event['elapsed_s']:8.3f}s]"
    if kind == 'rx':
        print(value['text'], end='', flush=True)
    elif kind == 'command':
        print(f'\n{stamp} HOST -> {value}', flush=True)
    else:
        print(f'\n{stamp} {kind.upper()}: {json.dumps(value, ensure_ascii=False)}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    sub.add_parser('ports')
    sub.add_parser('plan')
    run = sub.add_parser('run')
    run.add_argument('case', choices=CASES)
    run.add_argument('--port', required=True)
    run.add_argument('--chip', choices=('p4', 'c5'), default='p4')
    run.add_argument('--label', default='')
    run.add_argument('--notes', default='')
    run.add_argument('--cycles', type=int, default=1)
    run.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    if args.action == 'ports':
        print(json.dumps(ports(), indent=2))
    elif args.action == 'plan':
        print(json.dumps(CASES, indent=2, ensure_ascii=False))
    else:
        import signal
        runner = Runner(args.output, terminal_event)
        signal.signal(signal.SIGINT, lambda *_: runner.cancelled.set())
        signal.signal(signal.SIGTERM, lambda *_: runner.cancelled.set())
        report = runner.run(args.case, args.port, args.label, args.notes, args.chip, args.cycles)
        print(report['status'], report['output'])
        return 0 if report['status'] == 'complete' else 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
