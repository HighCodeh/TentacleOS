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

"""One esptool connection for the full guarded update and final reset."""
from contextlib import contextmanager, redirect_stdout
import io
import re
import signal
import sys
import time

ROM_BAUD = 115200
CONSOLE_WAIT_SECONDS = 12
BOOT_SETTLE_SECONDS = 1
CONSOLE_COLUMNS = 80
CONSOLE_WAKE_SECONDS = 1


class ConsoleTerminal:
    """Answer linenoise terminal queries, including fragmented UART sequences."""
    def __init__(self, connection):
        self.connection = connection
        self.pending = b''
        self.column = 1

    def receive(self, data):
        self.pending += data
        answered = False
        while self.pending:
            start = self.pending.find(b'\x1b')
            if start < 0:
                self.pending = b''
                break
            self.pending = self.pending[start:]
            if self.pending == b'\x1b' or self.pending.startswith(b'\x1b[') and not re.search(rb'[A-Za-z~]', self.pending[2:]):
                break
            match = re.match(rb'\x1b\[([0-9;?]*)([A-Za-z~])', self.pending)
            if not match:
                self.pending = self.pending[1:]
                continue
            params, operation = match.groups()
            self.pending = self.pending[match.end():]
            if operation == b'C' and params == b'999':
                self.column = CONSOLE_COLUMNS
            elif operation == b'D' and (not params or params.isdigit()):
                self.column = max(1, self.column - int(params or b'1'))
            elif operation == b'n' and params == b'6':
                self.connection.write(f'\x1b[1;{self.column}R'.encode())
                answered = True
            elif operation == b'n' and params == b'5':
                self.connection.write(b'\x1b[0n')
                answered = True
        return answered or bool(self.pending)


def request_console_command(port, command, acknowledgment):
    import serial
    connection = serial.Serial(port=None, baudrate=ROM_BAUD, timeout=0.1, write_timeout=1)
    connection.port = port
    connection.dtr = False
    connection.rts = False
    try:
        connection.open()
        terminal = ConsoleTerminal(connection)
        connection.write(b'\x15\r\n')
        until = time.monotonic() + CONSOLE_WAIT_SECONDS
        wake_at = time.monotonic() + CONSOLE_WAKE_SECONDS
        has_received = False
        received = b''
        while time.monotonic() < until:
            chunk = connection.read(4096)
            has_received = has_received or bool(chunk)
            if not has_received and time.monotonic() >= wake_at:
                # A previous monitor may have closed with a cursor query pending.
                connection.write(b'\x1b[1;1R\x15\r')
                wake_at = until
            received = (received + chunk)[-8192:]
            waiting = terminal.receive(chunk)
            if b'highboy>' in received and not waiting:
                connection.write((command + '\r\n').encode())
                until = time.monotonic() + CONSOLE_WAIT_SECONDS
                received = b''
                while time.monotonic() < until:
                    chunk = connection.read(4096)
                    terminal.receive(chunk)
                    received = (received + chunk)[-8192:]
                    if acknowledgment in received:
                        return True
                return False
        return False
    except serial.SerialException:
        return False
    finally:
        connection.close()


def request_console_boot(port, target):
    acknowledgment = b'Entering P4 ROM download mode' if target == 'p4' else b'C5 ROM boot: ESP_OK'
    if not request_console_command(port, f'{target} boot', acknowledgment):
        return False
    print(f'{target.upper()} console request entered download mode.', flush=True)
    time.sleep(BOOT_SETTLE_SECONDS)
    return True


def confirm_c5_boot(port):
    print('Waiting for P4 to confirm the new C5 over SPI...', flush=True)
    if not request_console_command(port, 'c5 wait 10', b'C5 link: ALIVE'):
        raise RuntimeError('C5 boot confirmation failed; do not advance to P4 flashing. '
                           'The C5 may roll back to its previous application.')
    # Allow the C5 health task to commit VALID after the acknowledged SPI command.
    time.sleep(BOOT_SETTLE_SECONDS)
    print('C5 SPI link restored before advancing to the next target.', flush=True)


def request_p4_boot(port):
    return request_console_boot(port, 'p4')


def request_c5_boot(args):
    control = getattr(args, 'control_port', None)
    if not control:
        from flash_ports import peer_port
        control = peer_port(args.port, 'p4')
    if not control or control == args.port:
        return False
    return request_console_boot(control, 'c5')


@contextmanager
def deadline(seconds):
    def expired(signum, frame):
        raise TimeoutError('esptool operation timed out; backup retained')
    previous = signal.signal(signal.SIGALRM, expired)
    timer = signal.setitimer(signal.ITIMER_REAL, seconds)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, *timer)
        signal.signal(signal.SIGALRM, previous)


class FlashSession:
    def __init__(self, args):
        self.args = args
        self.esp = None
        self.connected = False
        self.ready = False

    def connect(self):
        import esptool
        before = getattr(self.args, 'before', 'default-reset')
        if self.args.target == 'p4' and before == 'default-reset' and request_p4_boot(self.args.port):
            before = 'no-reset'
        if self.args.target == 'c5' and before == 'default-reset' and request_c5_boot(self.args):
            before = 'no-reset'
        self.esp = esptool.CHIP_DEFS[f'esp32{self.args.target}'](self.args.port, ROM_BAUD)
        try:
            self.esp.connect(before)
        except esptool.FatalError:
            if self.args.baud == ROM_BAUD:
                raise
            # A previous interrupted updater may have left the loader at its high baud.
            print(f'Retrying existing loader at {self.args.baud} baud without reset...', flush=True)
            self.esp._port.baudrate = self.args.baud
            if not self.esp._port.is_open:
                self.esp._port.open()
            self.esp.connect('no-reset', attempts=2)
        self.connected = True
        self.esp = esptool.run_stub(self.esp)
        if self.args.baud != self.esp._port.baudrate:
            self.esp.change_baud(self.args.baud)
        self.ready = True

    def run(self, command, timeout):
        import esptool
        with deadline(timeout):
            if not self.ready:
                self.connect()
            baud = (getattr(self.args, 'read_baud', self.args.baud) if command[0] in ('read_flash', 'read-flash')
                    else getattr(self.args, 'write_baud', self.args.baud) if command[0] in ('write_flash', 'write-flash')
                    else self.args.baud)
            if baud != self.args.baud:
                self.esp.change_baud(baud)
                self.args.baud = baud
            # The stub is already running; reuse it without uploading again.
            argv = ['--chip', f'esp32{self.args.target}', '--baud', str(self.args.baud),
                    '--before', 'no-reset-no-sync', '--after', 'no-reset', '--no-stub',
                    command[0].replace('_', '-'), *command[1:]]
            capture = command[0] in ('get_security_info', 'flash_id')
            output = io.StringIO()
            try:
                if capture:
                    with redirect_stdout(output):
                        esptool.main(argv, esp=self.esp)
                else:
                    esptool.main(argv, esp=self.esp)
            except SystemExit as error:
                if error.code:
                    raise RuntimeError(f'esptool failed (exit {error.code}); backup retained') from error
            finally:
                if capture:
                    print(output.getvalue(), end='', flush=True)
            return output.getvalue()

    def close(self, timeout):
        import esptool
        try:
            if self.connected:
                print('Resetting target...', flush=True)
                mode = 'watchdog-reset'
                with deadline(timeout):
                    esptool.cmds.reset_chip(self.esp, mode)
        finally:
            if self.esp is not None:
                self.esp._port.close()
            self.esp = None
            self.connected = False
            self.ready = False
