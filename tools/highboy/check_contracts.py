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

"""Offline source contract gates; no device access and no credentials read."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

C5_MEMORY = {
    "CONFIG_IDF_TARGET": '"esp32c5"',
    "CONFIG_FREERTOS_HZ": "1000",
    "CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL": "y",
    "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL": "65536",
    "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL": "1024",
    "CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM": "4",
    "CONFIG_ESP_WIFI_RX_BA_WIN": "4",
    "CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC": "y",
}


def config_values(paths):
    values = {}
    for path in paths:
        for line in path.read_text().splitlines():
            match = re.fullmatch(r"(CONFIG_\w+)=(.*)", line.strip())
            if match:
                values[match[1]] = match[2]
            match = re.fullmatch(r"# (CONFIG_\w+) is not set", line.strip())
            if match:
                values.pop(match[1], None)
    return values


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-built", choices=("p4", "c5"),
                        help="Require and validate this target's generated sdkconfig")
    args = parser.parse_args()
    headers = [(ROOT / f"firmware_{chip}/components/Service/spi_bridge/include/spi_protocol.h").read_bytes()
               for chip in ("p4", "c5")]
    require(headers[0] == headers[1], "P4/C5 SPI contract mismatch")
    require(re.search(rb"#define\s+SPI_PROTOCOL_VERSION\s+2\b", headers[0]), "Unexpected SPI version")
    require(re.search(rb"#define\s+SPI_TRANSPORT_REVISION\s+1\b", headers[0]), "Unexpected SPI transport revision")
    for chip, slot in (("p4", 0x2d0000), ("c5", 0x200000)):
        project = ROOT / f"firmware_{chip}"
        roots = {p.name for p in (project / "components").iterdir() if p.is_dir()}
        require(roots <= {"Applications", "Core", "Drivers", "Service"}, "Component outside architecture roots")
        rows = [line.split(",") for line in (project / "partitions.csv").read_text().splitlines()
                if line.strip() and not line.lstrip().startswith("#")]
        ota = [row for row in rows if row[0].strip() == "ota_0"]
        require(len(ota) == 1, "Missing OTA partition")
        value = ota[0][4].strip()
        size = int(value[:-1]) * 1024**2 if value.endswith("M") else int(value, 0)
        require(size == slot, "Unexpected partition migration")
        default_paths = [project / "sdkconfig.defaults"]
        defaults = config_values(default_paths)
        require(defaults.get("CONFIG_PARTITION_TABLE_OFFSET") == "0x8000", "Unexpected partition offset")
        if chip == "c5":
            for key, expected in C5_MEMORY.items():
                if key != "CONFIG_IDF_TARGET":
                    require(defaults.get(key) == expected, f"C5 defaults mismatch: {key}")
        config = project / "build_highboy/sdkconfig"
        if args.require_built == chip:
            require(config.is_file(), f"Missing generated {chip} sdkconfig: build validation required")
        if config.is_file():
            effective = config_values([config])
            require(effective.get("CONFIG_IDF_TARGET") == f'"esp32{chip}"', "Wrong compiled target")
            for key in ("SECURE_BOOT", "SECURE_FLASH_ENC_ENABLED", "SECURE_SIGNED_APPS_NO_SECURE_BOOT"):
                require(effective.get(f"CONFIG_{key}") != "y", "Unexpected security migration")
            if chip == "c5":
                for key, expected in C5_MEMORY.items():
                    require(effective.get(key) == expected, f"C5 generated configuration mismatch: {key}")
            print(f"{chip} generated configuration verified.")
    print("Offline contracts OK: paired SPI v2/transport 1, architecture roots, original app slots, C5 memory policy.")


if __name__ == "__main__":
    main()
