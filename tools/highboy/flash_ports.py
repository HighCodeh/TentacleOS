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

"""Select a chip and resolve its CP2105 interface without opening serial ports."""
import os
import plistlib
import re
import select
import subprocess
import sys

CP2105_ID = (0x10C4, 0xEA70)
TARGET_BY_INTERFACE = {0: 'p4', 1: 'c5'}
ESCAPE_TIMEOUT = 0.15
REGISTRY_TIMEOUT = 10


def choose(title, labels, input_stream=None, output=None):
    input_stream = input_stream or sys.stdin
    output = output or sys.stderr
    if not input_stream.isatty() or not output.isatty():
        raise ValueError('Interactive selection needs a terminal; specify p4 or c5 and -p PORT.')
    import termios
    import tty
    fd = input_stream.fileno()
    previous = termios.tcgetattr(fd)
    selected = 0
    drawn = False
    try:
        tty.setcbreak(fd)
        output.write(title + '\nUse Up/Down and Enter. Esc cancels.\n\x1b[?25l')
        while True:
            if drawn:
                output.write(f'\x1b[{len(labels)}A')
            for index, label in enumerate(labels):
                marker = ' > ' if selected == index else '   '
                output.write('\r\x1b[2K' + marker + label + '\n')
            output.flush()
            drawn = True
            key = os.read(fd, 1)
            if key in (b'\r', b'\n'):
                return selected
            if key in (b'', b'\x03', b'\x04'):
                raise KeyboardInterrupt
            if key == b'\x1b':
                sequence = b''
                for _ in range(2):
                    if not select.select([fd], [], [], ESCAPE_TIMEOUT)[0]:
                        break
                    sequence += os.read(fd, 1)
                if sequence in (b'[A', b'OA'):
                    selected = (selected - 1) % len(labels)
                elif sequence in (b'[B', b'OB'):
                    selected = (selected + 1) % len(labels)
                elif not sequence:
                    raise KeyboardInterrupt
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, previous)
        output.write('\x1b[?25h')
        output.flush()


def registry_interfaces(nodes):
    result = {}

    def walk(node, usb_id=None, interface=None):
        if not isinstance(node, dict):
            return
        if 'idVendor' in node and 'idProduct' in node:
            usb_id = (node['idVendor'], node['idProduct'])
            interface = None
        if 'bInterfaceNumber' in node:
            interface = node['bInterfaceNumber']
        port = node.get('IOCalloutDevice')
        if port and usb_id == CP2105_ID and interface in TARGET_BY_INTERFACE:
            result[port] = TARGET_BY_INTERFACE[interface]
        for child in node.get('IORegistryEntryChildren', []):
            walk(child, usb_id, interface)

    for node in nodes:
        walk(node)
    return result


def mac_interfaces():
    if sys.platform != 'darwin':
        return {}
    try:
        result = subprocess.run(['ioreg', '-a', '-l', '-r', '-c', 'IOUSBHostDevice'],
                                capture_output=True, check=True, timeout=REGISTRY_TIMEOUT)
        return registry_interfaces(plistlib.loads(result.stdout))
    except (OSError, ValueError, plistlib.InvalidFileException, subprocess.SubprocessError):
        return {}


def port_target(port, mac_ports):
    if (port.vid, port.pid) != CP2105_ID:
        return None
    evidence = set()
    if port.device in mac_ports:
        evidence.add(mac_ports[port.device])
    interface = (port.interface or '').lower()
    if re.search(r'\b(eci|enhanced)\b', interface):
        evidence.add('p4')
    if re.search(r'\b(sci|standard)\b', interface):
        evidence.add('c5')
    # Linux's physical location suffix is configuration.interface, not tty order.
    match = re.search(r':\d+\.([01])$', port.location or '')
    if match:
        evidence.add(TARGET_BY_INTERFACE[int(match[1])])
    match = re.search(r'\bMI_0([01])\b', port.hwid or '', re.IGNORECASE)
    if match:
        evidence.add(TARGET_BY_INTERFACE[int(match[1])])
    return next(iter(evidence)) if len(evidence) == 1 else None


def discover(target):
    from serial.tools import list_ports
    mac_ports = mac_interfaces()
    matches = {}
    for port in list_ports.comports():
        if port_target(port, mac_ports) == target:
            matches[port.device] = port
    return [matches[name] for name in sorted(matches)]


def peer_port(selected_port, target):
    """Resolve the other interface of the same physical CP2105, never tty order."""
    from serial.tools import list_ports
    inventory = list(list_ports.comports())
    boards = {}
    if sys.platform == 'darwin':
        try:
            result = subprocess.run(['ioreg', '-a', '-l', '-r', '-c', 'IOUSBHostDevice'],
                                    capture_output=True, check=True, timeout=REGISTRY_TIMEOUT)
            def walk(node, board=None):
                # Serial drivers repeat VID/PID but have separate registry IDs.
                if node.get('IOObjectClass') == 'IOUSBHostDevice':
                    board = None
                if (node.get('IOObjectClass') == 'IOUSBHostDevice' and
                        (node.get('idVendor'), node.get('idProduct')) == CP2105_ID):
                    board = node.get('locationID', node.get('IORegistryEntryID'))
                if board is not None and node.get('IOCalloutDevice'):
                    boards[node['IOCalloutDevice']] = board
                for child in node.get('IORegistryEntryChildren', []):
                    walk(child, board)
            for node in plistlib.loads(result.stdout):
                walk(node)
        except (OSError, ValueError, plistlib.InvalidFileException, subprocess.SubprocessError):
            pass
    def identity(port):
        if port.device in boards:
            return ('registry', boards[port.device])
        if port.location:
            return ('location', re.sub(r':\d+\.[01]$', '', port.location))
        if port.serial_number:
            return ('serial', port.serial_number)
        return None
    selected = next((p for p in inventory if p.device == selected_port), None)
    if selected is None or identity(selected) is None:
        return None
    mac_ports = mac_interfaces()
    matches = [p.device for p in inventory if p.device != selected_port and
               identity(p) == identity(selected) and port_target(p, mac_ports) == target]
    return matches[0] if len(matches) == 1 else None


def resolve_port(args):
    # Manual selection never enumerates or probes other devices.
    if any(arg in ('-p', '--port') or arg.startswith('--port=') or
           (arg.startswith('-p') and len(arg) > 2) for arg in args):
        return args
    target = 'p4'
    for index, arg in enumerate(args):
        if arg == '--target' and index + 1 < len(args):
            target = args[index + 1]
        elif arg.startswith('--target='):
            target = arg.split('=', 1)[1]
    if target not in ('p4', 'c5'):
        raise ValueError('Choose p4 or c5.')
    ports = discover(target)
    if not ports:
        raise ValueError(f'No identifiable Highboy {target.upper()} USB interface found. '
                         'Connect Highboy in UART mode, or specify -p PORT explicitly.')
    if len(ports) > 1:
        labels = [f'{port.device} | serial {port.serial_number or "unknown"} | '
                  f'USB {port.location or "unknown"}' for port in ports]
        port = ports[choose(f'Select Highboy {target.upper()}', labels)]
    else:
        port = ports[0]
    print(f'{target.upper()} port: {port.device} (CP2105 USB interface)', file=sys.stderr)
    return [*args, '-p', port.device]


def resolve_mode(args):
    for index, arg in enumerate(args):
        if arg == '--mode' or arg.startswith('--mode='):
            value = args[index + 1] if arg == '--mode' and index + 1 < len(args) else arg.partition('=')[2]
            if value not in ('app', 'full'):
                raise ValueError('--mode must be app or full.')
            return args
    if not sys.stdin.isatty() or not sys.stderr.isatty():
        raise ValueError('Specify --mode app or --mode full for non-interactive flashing.')
    mode = ('app', 'full')[choose('Flash mode (Full flash replaces partitions and assets)',
                                 ['Application only', 'Full flash'])]
    return [*args, '--mode', mode]


if __name__ == '__main__':
    try:
        if sys.argv[1:] != ['--select-target']:
            raise ValueError('Use --select-target.')
        print(('p4', 'c5', 'both')[choose('Which chip do you want to flash?',
                                 ['ESP32-P4', 'ESP32-C5', 'Both chips'])])
    except KeyboardInterrupt:
        print('\nCancelled; no flash started.', file=sys.stderr)
        sys.exit(130)
    except ValueError as error:
        print(error, file=sys.stderr)
        sys.exit(2)
