#!/usr/bin/env python3
"""Offline source contract gates; no device access and no credentials read."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    headers = [(ROOT / f"firmware_{chip}/components/Service/spi_bridge/include/spi_protocol.h").read_bytes()
               for chip in ("p4", "c5")]
    require(headers[0] == headers[1], "P4/C5 SPI contract mismatch")
    require(re.search(rb"#define\s+SPI_PROTOCOL_VERSION\s+8\b", headers[0]), "Unexpected SPI version")
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
        overlay = "sdkconfig.muse" if chip == "p4" else "sdkconfig.baseline"
        defaults = (project / "sdkconfig.defaults").read_text() + (project / overlay).read_text()
        require("CONFIG_PARTITION_TABLE_OFFSET=0x8000" in defaults, "Unexpected partition offset")
        if chip == "c5":
            require("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536" in defaults, "C5 DMA reserve missing")
            require("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y" in defaults, "C5 BLE PSRAM policy missing")
        config = project / "build_highboy/sdkconfig"
        if config.exists():
            effective = config.read_text().splitlines()
            require(f'CONFIG_IDF_TARGET="esp32{chip}"' in effective, "Wrong compiled target")
            for key in ("SECURE_BOOT", "SECURE_FLASH_ENC_ENABLED", "SECURE_SIGNED_APPS_NO_SECURE_BOOT"):
                require(f"CONFIG_{key}=y" not in effective, "Unexpected security migration")
    print("Offline contracts OK: paired SPI v8, architecture roots, original app slots, C5 memory policy.")


if __name__ == "__main__":
    main()
