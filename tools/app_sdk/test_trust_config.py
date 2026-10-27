#!/usr/bin/env python3
"""Exercise real CMake trust selection and compile its public-key headers offline."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "firmware_p4/components/Service/app_runtime"
# RFC 8032 test-vector public key; never a production identity.
TEST_KEY = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"


class TrustConfigTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="tos-trust-test-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.source = self.directory / "source"
        self.source.mkdir()
        self.build = self.directory / "build"
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\n'
            'project(trust NONE)\n'
            'set(GENERATED_VERSION_DIR "${CMAKE_BINARY_DIR}/generated")\n'
            f'include("{RUNTIME}/trust_keys.cmake")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${TOS_APP_HAS_CUSTOM_TRUST}")\n')
        header = (RUNTIME / "include/tos_trust_keys.h").read_text()
        self.development_key = "".join(re.findall(r"0x([0-9a-fA-F]{2})", header)).lower()
        self.assertEqual(len(self.development_key), 64)

    def configure(self, production="OFF", key="", is_valid=True):
        result = subprocess.run(
            ["cmake", "-S", str(self.source), "-B", str(self.build),
             f"-DTOS_APP_PRODUCTION={production}", f"-DTOS_APP_TRUST_PUBLIC_KEY={key}"],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode == 0, is_valid, result.stdout + result.stderr)
        return result

    def compile_key(self, expected, is_custom):
        header = "tos_build_trust_keys.h" if is_custom else "tos_trust_keys.h"
        values = ", ".join(f"0x{expected[index:index + 2]}" for index in range(0, 64, 2))
        source = self.directory / "check.c"
        source.write_text(
            '#include <string.h>\n'
            f'#include "{header}"\n'
            'int main(void) {\n'
            f'  const unsigned char expected[32] = {{{values}}};\n'
            '  return TOS_TRUST_KEY_COUNT != 1 ||\n'
            '         memcmp(TOS_TRUST_KEYS[0], expected, sizeof(expected));\n}\n')
        executable = self.directory / "check"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-I", str(self.build / "generated"),
                        "-I", str(RUNTIME / "include"), str(source), "-o", str(executable)],
                       check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=30)

    def test_default_keeps_development_identity(self):
        self.configure()
        self.assertEqual((self.build / "selected.txt").read_text(), "OFF")
        self.assertFalse((self.build / "generated/tos_build_trust_keys.h").exists())
        self.compile_key(self.development_key, False)

    def test_production_requires_key(self):
        result = self.configure("ON", is_valid=False)
        self.assertIn("requires TOS_APP_TRUST_PUBLIC_KEY", result.stderr)

    def test_production_rejects_shipped_development_key(self):
        for key in (self.development_key, self.development_key.upper()):
            with self.subTest(key=key):
                result = self.configure("ON", key, is_valid=False)
                self.assertIn("cannot be used for production", result.stderr)

    def test_invalid_keys_fail(self):
        for key in ("0" * 64, "a" * 63, "a" * 65, "g" * 64, "a" * 32 + ";" + "a" * 31):
            with self.subTest(key=key):
                self.configure("ON", key, is_valid=False)

    def test_production_contains_only_selected_key(self):
        self.configure("ON", TEST_KEY.upper())
        self.assertEqual((self.build / "selected.txt").read_text(), "ON")
        self.compile_key(TEST_KEY, True)

    def test_reconfigure_switches_key_and_returns_to_development(self):
        self.configure("ON", TEST_KEY)
        self.compile_key(TEST_KEY, True)
        self.configure("OFF", self.development_key)
        self.compile_key(self.development_key, True)
        self.configure()
        self.assertEqual((self.build / "selected.txt").read_text(), "OFF")
        self.compile_key(self.development_key, False)
        # Reusing the build directory must not bypass the production gate.
        self.configure("ON", is_valid=False)


if __name__ == "__main__":
    unittest.main()
