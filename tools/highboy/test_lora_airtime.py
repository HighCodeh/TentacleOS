#!/usr/bin/env python3
"""Test production airtime accounting with fake time and a real host mutex."""
from concurrent.futures import ThreadPoolExecutor
import ctypes
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "firmware_p4/components/Drivers/sx1262"


def main():
    with tempfile.TemporaryDirectory(prefix="tos-airtime-") as temporary:
        directory = Path(temporary)
        (directory / "freertos").mkdir()
        (directory / "esp_err.h").write_text(
            'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n'
            '#define ESP_ERR_INVALID_SIZE 2\n#define ESP_ERR_TIMEOUT 3\n')
        (directory / "esp_log.h").write_text('#define ESP_LOGD(tag, ...) ((void)(tag))\n')
        (directory / "esp_timer.h").write_text(
            '#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
        (directory / "freertos/FreeRTOS.h").write_text(
            '#include <pthread.h>\ntypedef pthread_mutex_t portMUX_TYPE;\n'
            '#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER\n'
            '#define portENTER_CRITICAL(lock) pthread_mutex_lock(lock)\n'
            '#define portEXIT_CRITICAL(lock) pthread_mutex_unlock(lock)\n')
        (directory / "fixture.c").write_text('''
#include <stdatomic.h>
#include "sx1262_airtime.h"
#include "sx1262_regs.h"
static atomic_int_fast64_t s_now;
int64_t esp_timer_get_time(void) { return atomic_load(&s_now); }
void set_time(int64_t now) { atomic_store(&s_now, now); }
static sx1262_config_t config(uint32_t freq, uint8_t sf, uint8_t bw) {
  sx1262_config_t cfg = {.frequency_hz=freq, .sf=sf, .bw=bw,
    .cr=SX1262_LORA_CR_4_5, .preamble_len=8, .is_crc_on=true};
  return cfg;
}
uint64_t estimate(uint8_t sf, uint8_t bw, uint8_t length) {
  sx1262_config_t cfg = config(868100000, sf, bw);
  return sx1262_airtime_estimate_us(&cfg, length);
}
int reserve(uint32_t frequency, uint8_t sf, uint8_t bw, uint8_t length) {
  sx1262_config_t cfg = config(frequency, sf, bw);
  return sx1262_airtime_reserve(&cfg, length);
}
uint64_t invalid(void) { return sx1262_airtime_estimate_us(NULL, 20); }
''')
        library = directory / ("airtime.dylib" if sys.platform == "darwin" else "airtime.so")
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                        "-Werror", "-shared", "-fPIC", "-pthread",
                        "-I", str(directory), "-I", str(DRIVER / "include"),
                        str(DRIVER / "sx1262_airtime.c"), str(directory / "fixture.c"),
                        "-o", str(library)], check=True)
        lib = ctypes.CDLL(str(library))
        lib.set_time.argtypes = [ctypes.c_int64]
        lib.estimate.argtypes = [ctypes.c_uint8] * 3
        lib.estimate.restype = ctypes.c_uint64
        lib.reserve.argtypes = [ctypes.c_uint32] + [ctypes.c_uint8] * 3
        lib.invalid.restype = ctypes.c_uint64
        assert lib.estimate(7, 4, 20) == 56576
        assert lib.estimate(12, 4, 20) == 1318912
        assert lib.estimate(7, 4, 255) > lib.estimate(7, 4, 20)
        assert lib.invalid() == 0
        assert lib.estimate(4, 4, 20) == 0
        assert lib.estimate(13, 4, 20) == 0
        assert lib.estimate(7, 7, 20) == 0
        assert lib.estimate(7, 4, 0) == 0
        now = 100000000000
        def check_interval(frequency, sf, bw, divisor):
            nonlocal now
            now += 1000000000000
            lib.set_time(now)
            airtime = lib.estimate(sf, bw, 20)
            assert lib.reserve(frequency, sf, bw, 20) == 0
            lib.set_time(now + airtime * divisor - 1)
            assert lib.reserve(frequency, sf, bw, 20) == 3
            lib.set_time(now + airtime * divisor)
            assert lib.reserve(frequency, sf, bw, 20) == 0
        check_interval(868100000, 7, 4, 100)
        check_interval(869525000, 7, 4, 10)
        check_interval(433920000, 7, 4, 10)
        check_interval(868900000, 7, 4, 1000)
        # BW250 does not fit wholly into the 869.4-869.65 MHz fast subband.
        check_interval(869450000, 7, 5, 1000)
        now += 1000000000000
        lib.set_time(now)
        with ThreadPoolExecutor(max_workers=16) as executor:
            results = list(executor.map(lambda _: lib.reserve(868100000, 7, 4, 20), range(64)))
        assert results.count(0) == 1 and results.count(3) == 63
        # Switching frequency/protocol/config must not erase the previous debit.
        assert lib.reserve(869525000, 7, 4, 20) == 3
        assert lib.reserve(433920000, 7, 4, 20) == 3
        # Other bands do not debit or clear this conservative EU guard.
        assert lib.reserve(915000000, 7, 4, 20) == 0
        assert lib.reserve(915000000, 7, 4, 20) == 0
        assert lib.reserve(868100000, 7, 4, 20) == 3
        source = (DRIVER / "sx1262_radio.c").read_text()
        transmit = source[source.index("esp_err_t sx1262_radio_transmit"):source.index(
            "esp_err_t sx1262_radio_receive_single")]
        assert transmit.index("sx1262_airtime_reserve") < transmit.index("sx1262_fsm_get_state")
        assert "sx1262_airtime" not in source[source.index("void sx1262_radio_init"):source.index(
            "esp_err_t sx1262_radio_transmit")]
        print("PASS: LoRa airtime vectors, occupied bandwidth, exact cooldown, concurrency and retained debit")


if __name__ == "__main__":
    main()
