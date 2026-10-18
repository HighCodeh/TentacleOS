#!/usr/bin/env python3
"""Exercise generated configuration gates without modifying build outputs."""
import contextlib
import io
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

import check_contracts

REPO = check_contracts.ROOT


class ContractTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="highboy-contract-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for chip, overlay in (("p4", "sdkconfig.muse"), ("c5", "sdkconfig.baseline")):
            project = self.root / f"firmware_{chip}"
            project.mkdir()
            for name in ("sdkconfig.defaults", overlay, "partitions.csv",
                         "components/Service/spi_bridge/include/spi_protocol.h"):
                target = project / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(REPO / f"firmware_{chip}" / name, target)
        self.addCleanup(patch.stopall)
        patch.object(check_contracts, "ROOT", self.root).start()

    def run_check(self, *args):
        with patch("sys.argv", ["check_contracts.py", *args]), contextlib.redirect_stdout(io.StringIO()):
            check_contracts.main()

    def write_config(self, chip, values):
        config = self.root / f"firmware_{chip}/build_highboy/sdkconfig"
        config.parent.mkdir(exist_ok=True)
        config.write_text("\n".join(f"{key}={value}" for key, value in values.items()) + "\n")
        return config

    def test_source_only_without_builds(self):
        self.run_check()

    def test_missing_required_build_fails(self):
        for chip in ("p4", "c5"):
            with self.subTest(chip=chip), self.assertRaisesRegex(SystemExit, "Missing generated"):
                self.run_check("--require-built", chip)

    def test_valid_generated_configs(self):
        self.write_config("p4", {"CONFIG_IDF_TARGET": '"esp32p4"'})
        self.write_config("c5", check_contracts.C5_MEMORY)
        for chip in ("p4", "c5"):
            self.run_check("--require-built", chip)

    def test_each_c5_setting_is_required(self):
        for key in check_contracts.C5_MEMORY:
            with self.subTest(key=key):
                values = dict(check_contracts.C5_MEMORY)
                del values[key]
                self.write_config("c5", values)
                with self.assertRaises(SystemExit):
                    self.run_check("--require-built", "c5")

    def test_each_c5_setting_is_checked(self):
        for key in check_contracts.C5_MEMORY:
            with self.subTest(key=key):
                values = dict(check_contracts.C5_MEMORY)
                values[key] = "incorrect"
                self.write_config("c5", values)
                with self.assertRaises(SystemExit):
                    self.run_check("--require-built", "c5")

    def test_disabled_c5_setting_fails(self):
        config = self.write_config("c5", check_contracts.C5_MEMORY)
        with config.open("a") as output:
            output.write("# CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL is not set\n")
        with self.assertRaisesRegex(SystemExit, "CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL"):
            self.run_check("--require-built", "c5")

    def test_overlay_cannot_override_memory_policy(self):
        overlay = self.root / "firmware_c5/sdkconfig.baseline"
        with overlay.open("a") as output:
            output.write("\nCONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=1\n")
        with self.assertRaisesRegex(SystemExit, "C5 defaults mismatch"):
            self.run_check()

    def test_security_migration_fails(self):
        self.write_config("p4", {"CONFIG_IDF_TARGET": '"esp32p4"', "CONFIG_SECURE_BOOT": "y"})
        with self.assertRaisesRegex(SystemExit, "security migration"):
            self.run_check("--require-built", "p4")


if __name__ == "__main__":
    unittest.main()
