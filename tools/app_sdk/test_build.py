#!/usr/bin/env python3
"""Exercise the actual SDK shell script with isolated compiler/signing fixtures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildTests(unittest.TestCase):
    def test_portable_size_and_external_key(self):
        with tempfile.TemporaryDirectory(prefix="tos sdk build ") as temporary:
            directory = Path(temporary)
            sdk = directory / "sdk"
            sdk.mkdir()
            shutil.copyfile(ROOT / "tools/app_sdk/build.sh", sdk / "build.sh")
            app = sdk / "hello"
            app.mkdir()
            (app / "app.c").write_text("/* Compiler fixture input. */\n")
            binary = directory / "bin"
            binary.mkdir()
            fixtures = {
                "riscv32-esp-elf-gcc": '''#!/bin/bash
set -eu
while [ "$#" -gt 0 ]; do
  if [ "$1" = -o ]; then shift; printf ELF > "$1"; exit; fi
  shift
done
exit 1
''',
                "signer": '''#!/bin/bash
set -eu
[ "$8" = --sign ]
[ "$9" = "$EXPECTED_KEY" ]
printf HBAPtest > "$3"
''',
                "stat": '#!/bin/bash\necho "GNU stat must not be used" >&2\nexit 99\n',
            }
            for name, source in fixtures.items():
                path = binary / name
                path.write_text(source)
                path.chmod(0o755)
            key = str(directory / "external signing seed")
            env = dict(os.environ, PATH=str(binary) + os.pathsep + os.environ["PATH"],
                       SYSTEM_PYTHON=str(binary / "signer"), TOS_APP_SIGNING_KEY=key,
                       EXPECTED_KEY=key)
            result = subprocess.run(["bash", str(sdk / "build.sh")], env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("(8 bytes)", result.stdout)
            self.assertEqual(result.stderr, "")
            self.assertEqual((app / "app.hb").read_bytes(), b"HBAPtest")


if __name__ == "__main__":
    unittest.main()
