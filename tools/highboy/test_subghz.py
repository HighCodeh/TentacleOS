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

"""Compile and run the real Sub-GHz codecs with RAM-only hardware/storage fakes."""
from pathlib import Path
import os
import argparse
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT / "firmware_p4/components/Applications/SubGhz"
STUBS = HERE / "test_subghz_stubs"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, help="Verify a local RAW file without transmitting")
    args = parser.parse_args()
    sources = sorted((SOURCE / "protocols").glob("*.c"))
    sources += [SOURCE / name for name in (
        "subghz_keeloq.c", "subghz_protocol_serializer.c", "subghz_replay.c", "subghz_raw_file.c")]
    sources.append(ROOT / "firmware_p4/components/Drivers/cc1101/cc1101_config.c")
    with tempfile.TemporaryDirectory(prefix="highboy-subghz-") as temp:
        binary = Path(temp) / "test_subghz"
        work = Path(temp)
        notice = (STUBS / "esp_heap_caps.h").read_text().split("#pragma", 1)[0]
        (work / "tos_storage_paths.h").write_text(notice + '#define TOS_PATH_SUBGHZ "' + temp + '/captures"\n')
        (work / "storage_mkdir.h").write_text(notice + '#include <sys/stat.h>\n#include <errno.h>\n#include "esp_err.h"\nstatic inline esp_err_t storage_mkdir_recursive(const char *path) { return mkdir(path, 0700) == 0 || errno == EEXIST ? ESP_OK : ESP_FAIL; }\n')
        (work / "esp_attr.h").write_text(notice + "#define EXT_RAM_BSS_ATTR\n")
        command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                   "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                   "-fno-sanitize-recover=all", "-g", "-I", str(STUBS),
                   "-I", str(work), "-I", str(SOURCE / "include"),
                   "-I", str(ROOT / "firmware_p4/components/Drivers/cc1101/include"),
                   "-I", str(SOURCE / "protocols/include"),
                   str(HERE / "test_subghz.c"), *map(str, sources), "-o", str(binary)]
        (work / "host_string.h").write_text(notice + '#include <string.h>\nstatic inline size_t fixture_strlcpy(char *dst, const char *src, size_t size) { size_t n = strlen(src); if (size) { size_t copy = n < size - 1 ? n : size - 1; memcpy(dst, src, copy); dst[copy] = 0; } return n; }\n#define strlcpy fixture_strlcpy\n')
        storage_object = work / "storage.o"
        storage_command = command[:command.index(str(HERE / "test_subghz.c"))]
        subprocess.run([*storage_command, "-include", str(work / "host_string.h"),
                        "-Dsubghz_storage_read=subghz_test_disk_read", "-c",
                        str(SOURCE / "subghz_storage.c"), "-o", str(storage_object)], check=True)
        command.insert(command.index("-o"), str(storage_object))
        subprocess.run(command, check=True)
        command = [str(binary)]
        if args.capture:
            command.append(str(args.capture.resolve()))
        result = subprocess.run(command, check=False, timeout=30, capture_output=True, text=True)
        if result.returncode:
            print(result.stdout)
            print(result.stderr)
            result.check_returncode()
        print("\n".join(line for line in result.stdout.splitlines() if not line.startswith("CAPTURE_")))
        if args.capture:
            lines = args.capture.read_text().splitlines()
            expected = [int(value) for line in lines if line.startswith("RAW_Data:")
                        for value in line.partition(":")[2].split()]
            fields = dict(line.split(": ", 1) for line in lines if ": " in line and not line.startswith("RAW_Data:"))
            presets = {"FuriHalSubGhzPresetOok270Async": 1, "FuriHalSubGhzPresetOok650Async": 2,
                       "FuriHalSubGhzPreset2FSKDev238Async": 3, "FuriHalSubGhzPreset2FSKDev476Async": 4}
            actual_lines = result.stdout.splitlines()
            actual = next(line for line in actual_lines if line.startswith("CAPTURE_PULSES:")).split()[1:]
            assert list(map(int, actual)) == expected, "Replay changed RAW pulse order, polarity or duration"
            metadata = next(line for line in actual_lines if line.startswith("CAPTURE_METADATA:")).split()[1:]
            assert list(map(int, metadata)) == [int(fields["Frequency"]), presets[fields["Preset"]]]
            print(f"PASS: external RAW: {len(expected)} pulses, {sum(map(abs, expected))} us, "
                  f"{fields['Frequency']} Hz; exact pulse-by-pulse replay and preset match (no RF)")


if __name__ == "__main__":
    main()
