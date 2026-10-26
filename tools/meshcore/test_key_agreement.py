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

"""Check production MeshCore key agreement against real libsodium."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / "firmware_p4/components/Applications/LoRa/meshcore/meshcore_crypto.c").read_text()
    signature = re.search(r"\besp_err_t\s+mc_x25519\s*\(", source)
    if signature is None:
        raise RuntimeError("Production mc_x25519 definition not found")
    start = signature.start()
    end = source.index("\n}", start) + 2
    flags = shlex.split(os.environ.get("HOST_LINK_TEST_CFLAGS", ""))
    libs = shlex.split(os.environ.get("HOST_LINK_TEST_LDFLAGS", ""))
    if not libs:
        flags += shlex.split(subprocess.check_output(["pkg-config", "--cflags", "libsodium"], text=True))
        libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "libsodium"], text=True))
    with tempfile.TemporaryDirectory(prefix="meshcore-keys-") as temporary:
        work = Path(temporary)
        notice = Path(__file__).read_text().split("\n\n", 1)[0].split("\n", 1)[1]
        preamble = "\n".join("//" + line[1:] for line in notice.splitlines()) + "\n"
        test = preamble + r'''#include <assert.h>
#include <string.h>
#include <sodium.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_LOGW(...) ((void)0)
''' + source[start:end] + r'''
int main(void) {
  assert(sodium_init() >= 0);
  uint8_t alice_pk[32], alice_sk[64], bob_pk[32], bob_sk[64], a[32], b[32];
  uint8_t invalid[32] = {0};
  assert(crypto_sign_keypair(alice_pk, alice_sk) == 0);
  assert(crypto_sign_keypair(bob_pk, bob_sk) == 0);
  assert(mc_x25519(a, bob_pk, alice_sk) == ESP_OK);
  assert(mc_x25519(b, alice_pk, bob_sk) == ESP_OK);
  assert(memcmp(a, b, sizeof(a)) == 0 && !sodium_is_zero(a, sizeof(a)));
  memset(a, 0xA5, sizeof(a));
  assert(mc_x25519(a, invalid, alice_sk) == ESP_ERR_INVALID_ARG);
  assert(sodium_is_zero(a, sizeof(a)));
  assert(mc_x25519(NULL, bob_pk, alice_sk) == ESP_ERR_INVALID_ARG);
  assert(mc_x25519(a, NULL, alice_sk) == ESP_ERR_INVALID_ARG);
  assert(mc_x25519(a, bob_pk, NULL) == ESP_ERR_INVALID_ARG);
  return 0;
}
'''
        (work / "test.c").write_text(test)
        executable = work / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall",
                        "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", *flags, str(work / "test.c"),
                        *libs, "-o", str(executable)], check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
    print("PASS: production MeshCore ECDH agrees for valid keys and rejects invalid peers")


if __name__ == "__main__":
    main()
