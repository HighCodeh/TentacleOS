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

"""Portable direct UART terminal; never sends an RF command automatically."""
import argparse
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Explicit P4 serial port, e.g. COM6 or /dev/cu.usbserial-...")
    args = parser.parse_args()
    try:
        from serial.tools import list_ports
    except ImportError:
        raise SystemExit("Install dependency: python -m pip install pyserial==3.5")
    ports = sorted(list_ports.comports(), key=lambda item: item.device)
    if not ports:
        raise SystemExit("No serial ports found. Connect High Boy with a USB data cable.")
    port = args.port
    if port is None:
        for index, item in enumerate(ports, 1):
            print(f"{index}. {item.device} | {item.description} | serial={item.serial_number}")
        print("Select the P4 UART. Windows COM numbers do not establish P4/C5 identity.")
        try:
            choice = int(input("Port number: "))
            if not 1 <= choice <= len(ports): raise ValueError()
        except (ValueError, EOFError):
            raise SystemExit("Invalid selection; no port opened.")
        port = ports[choice - 1].device
    if port not in [item.device for item in ports]:
        raise SystemExit(f"Port not connected: {port}")
    print("HIGH BOY RFID LAB | UART 115200 8N1 | flow control OFF | DTR/RTS released")
    print("Press Enter. Expected prompt: rfid>  (if different, exit and select P4).")
    print("help                 list commands")
    print("drive 125 50 5000     GPIO2 scope wave, 125 kHz, 50%, 5 s (other readers OFF)")
    print("sweep scope          GPIO2: 20 to 200 kHz, 10 kHz steps, 5 s each")
    print("pulse 1000 50 1000    GPIO11 pulse, 1 kHz, 50%, 1 second")
    print("listen 1000          passive measurement; both outputs LOW")
    print("read 5000            EM4100 RF/64 read, 5 seconds")
    print("off                  stop outputs; Ctrl+C also stops")
    print("Ctrl+] exits terminal. Type off and status before exiting.")
    print("No RF command is sent automatically.\n", flush=True)
    return subprocess.call([sys.executable, "-m", "serial.tools.miniterm", "--dtr", "0", "--rts", "0",
                            "--eol", "CRLF", "--raw", port, "115200"])


if __name__ == "__main__":
    raise SystemExit(main())
