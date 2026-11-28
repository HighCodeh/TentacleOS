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

"""Check production unknown-signal gating; optionally analyze private local captures."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from test_rf_ownership import function

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accept", type=Path)
    parser.add_argument("--reject", type=Path)
    args = parser.parse_args()
    source = (ROOT / "firmware_p4/components/Applications/SubGhz/subghz_receiver.c").read_text()
    notice = source.split("#include", 1)[0]
    defines = "\n".join(re.findall(r"^#define UNKNOWN_.*$", source, re.M))
    harness = notice + r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
""" + defines + "\n" + function(source, "static bool has_signal_timing")
    harness += r"""
int main(void) {
  int32_t good[50];
  for (unsigned i=0; i<48; i+=2) { good[i]=413; good[i+1]=-1239; }
  good[48]=413; good[49]=-12803;
  int32_t copy[50]; memcpy(copy, good, sizeof(good));
  assert(has_signal_timing(good, 50));
  assert(!memcmp(copy, good, sizeof(good)));
  assert(!has_signal_timing(good, 15));
  int32_t noise[64];
  for (unsigned i=0; i<64; i+=2) { noise[i]=33; noise[i+1]=-3000; }
  assert(!has_signal_timing(noise, 64));
  for (unsigned i=0; i<64; i++) noise[i]=400;
  assert(!has_signal_timing(noise, 64));
  for (unsigned i=0; i<64; i+=2) { noise[i]=INT32_MAX; noise[i+1]=INT32_MIN; }
  assert(!has_signal_timing(noise, 64));
  for (unsigned i=0; i<64; i+=2) { noise[i]=80; noise[i+1]=-80; }
  assert(has_signal_timing(noise, 64));
  assert(!has_signal_timing(NULL, 0));
"""
    for label, path, expected in (("accepted", args.accept, True), ("rejected", args.reject, False)):
        if path:
            pulses = [int(word) for line in path.read_text().splitlines()
                      if line.startswith("RAW_Data:") for word in line.split(":", 1)[1].split()]
            assert pulses
            harness += "{const int32_t pulses[]={" + ",".join(map(str, pulses)) + "};"
            harness += f"assert(has_signal_timing(pulses, {len(pulses)}) == {str(expected).lower()});}}\n"
            print(f"Checking {label} local capture: {path.name}, {len(pulses)} pulses")
    harness += "return 0;}\n"
    with tempfile.TemporaryDirectory(prefix="subghz-noise-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(harness)
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True, timeout=30)
    print("PASS: signal/noise quality, polarity, timing boundaries, overflow and unchanged input")


if __name__ == "__main__":
    main()
