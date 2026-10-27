#!/usr/bin/env python3
"""Exercise the production RX worker with explicit RMT/RTOS/resource fakes."""
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT / "firmware_p4/components/Applications/SubGhz"


def main():
    with tempfile.TemporaryDirectory(prefix="subghz-lifecycle-") as temporary:
        work = Path(temporary)
        headers = ["driver/gpio.h", "driver/rmt_rx.h", "driver/rmt_encoder.h",
                   "esp_log.h", "esp_err.h", "freertos/FreeRTOS.h", "freertos/task.h",
                   "freertos/queue.h", "freertos/semphr.h", "cc1101.h", "pin_def.h",
                   "led_control.h", "subghz_protocol_registry.h", "subghz_storage.h",
                   "subghz_transmitter.h", "resource_mgr.h", "sys_prio.h"]
        for name in headers:
            target = work / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('#include "rx_fakes.h"\n')
        (work / "rx_fakes.h").write_text((HERE / "test_subghz_rx_fakes.h").read_text())
        binary = work / "rx_test"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-Wno-unused-parameter", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", "-g", "-I", str(work), "-I", str(SOURCE),
                        "-I", str(SOURCE / "include"),
                        str(HERE / "test_subghz_lifecycle.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
