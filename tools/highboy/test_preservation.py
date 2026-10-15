#!/usr/bin/env python3
"""Offline import-preservation gates and canonical-source Muse host regressions.

Only public source, the import manifest and named non-secret build settings are
inspected. Host binaries use explicit fixtures and live in a temporary directory.
No firmware build, serial port, device, network or account API is used.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tools/highboy/fixtures/muse_port"
C5 = ROOT / "firmware_c5/components/Service/highboy_muse"
P4 = ROOT / "firmware_p4/components/Service/highboy_muse"
RUN_HOST = True

# These are intentional integration changes, NOT assertions of byte preservation.
# Their behavior is covered by the dedicated SubGHz tests and firmware builds.
REVIEWED_MERGES = {
    "firmware_p4/CMakeLists.txt",
    "firmware_c5/CMakeLists.txt",
    "firmware_p4/components/Applications/CMakeLists.txt",
    "firmware_p4/sdkconfig.defaults",
    "firmware_p4/main/idf_component.yml",
    "firmware_p4/components/Service/app_runtime/include/tos_api.h",
    "firmware_p4/components/Service/highboy_muse/p4_app/hb_muse_app.c",
    "firmware_p4/components/Applications/doom/doomgeneric_highboy.c",
    "firmware_p4/components/Service/storage_vfs/usb_msc.c",
    "firmware_p4/components/Service/console/commands/cmd_subghz.c",
    "firmware_p4/components/Applications/SubGhz/subghz_protocol_serializer.c",
    "firmware_p4/components/Applications/SubGhz/subghz_replay.c",
    "firmware_p4/components/Applications/SubGhz/protocols/protocol_ansonic.c",
    "firmware_p4/components/Applications/SubGhz/protocols/protocol_chamberlain.c",
    "firmware_p4/components/Applications/SubGhz/protocols/protocol_holtek.c",
    "firmware_p4/components/Applications/SubGhz/protocols/protocol_liftmaster.c",
    "firmware_p4/components/Applications/SubGhz/protocols/protocol_linear.c",
    "firmware_p4/components/Applications/SubGhz/protocols/protocol_nice_flo.c",
    "firmware_p4/components/Applications/SubGhz/protocols/protocol_princeton.c",
    "firmware_p4/components/Applications/SubGhz/protocols/protocol_rcswitch.c",
    "firmware_p4/components/Applications/SubGhz/protocols/subghz_protocol_registry.c",
    "firmware_p4/components/Applications/SubGhz/protocols/include/subghz_protocol_registry.h",
    "firmware_p4/components/Applications/SubGhz/protocols/include/subghz_protocol_utils.h",
}

REVIEWED_FIXTURES = {"pairing/test_setup.c", "p4_app/test_app.c"}

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


def digest(content):
    return hashlib.sha256(content).hexdigest()


def normalized(content):
    return content.replace(b"\r\n", b"\n")


def valid_hash(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def formatting_chain_matches(item, actual_hash, proof):
    """Accept only an explicit transformation of the sealed source, not any changed file."""
    if not isinstance(proof, dict):
        return False
    before, after = proof.get("before_sha256"), proof.get("after_sha256")
    if not valid_hash(before) or not valid_hash(after) or actual_hash != after:
        return False
    if not isinstance(proof.get("formatter"), str) or not proof["formatter"].strip():
        return False
    reviewed = item.get("reviewed_target_sha256")
    if reviewed is not None:
        return valid_hash(reviewed) and before == reviewed
    if before == item.get("local_sha256"):
        return True
    # A normalized upstream-equivalent source already has its exact upstream
    # bytes pinned. The normalized hash alone does not authorize a new input.
    return (item.get("action") == "upstream_equivalent"
            and valid_hash(item.get("upstream_sha256"))
            and before == item["upstream_sha256"])


def contained(base, relative):
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Unsafe manifest path")
    resolved = (base / path).resolve()
    resolved.relative_to(base.resolve())
    return resolved


def config_values(paths):
    values = {}
    for path in paths:
        for line in path.read_text().splitlines():
            if line.startswith("CONFIG_") and "=" in line:
                key, value = line.split("=", 1)
                if key in C5_MEMORY:
                    values[key] = value
            match = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
            if match and match[1] in C5_MEMORY:
                values.pop(match[1], None)
    return values


def fixture_expected_content(item):
    """Keep original evidence and require complete seals for explicitly reviewed fixtures."""
    if not valid_hash(item.get("sha256")) or type(item.get("bytes")) is not int or item["bytes"] < 0:
        raise ValueError("Invalid original fixture evidence")
    review_fields = {"reviewed_sha256", "reviewed_bytes", "review_reason"}
    present = review_fields.intersection(item)
    if item.get("path") not in REVIEWED_FIXTURES:
        if present:
            raise ValueError("Unlisted fixture review")
        return item["sha256"], item["bytes"]
    if present != review_fields:
        raise ValueError("Incomplete reviewed fixture evidence")
    if (not valid_hash(item["reviewed_sha256"])
            or type(item["reviewed_bytes"]) is not int or item["reviewed_bytes"] < 0
            or not isinstance(item["review_reason"], str) or not item["review_reason"].strip()):
        raise ValueError("Invalid reviewed fixture evidence")
    return item["reviewed_sha256"], item["reviewed_bytes"]


def verify_fixture_inventory():
    manifest = json.loads((FIXTURES / "PROVENANCE.json").read_text())
    records = manifest["files"]
    names = {item["path"] for item in records}
    if len(names) != len(records) or len(records) != 15:
        raise ValueError("Unexpected or duplicate host fixture inventory")
    actual = {str(path.relative_to(FIXTURES)) for path in FIXTURES.rglob("*")
              if path.is_file() and path.name != "PROVENANCE.json"}
    if actual != names:
        raise ValueError("Host fixture inventory differs from provenance")
    for item in records:
        content = contained(FIXTURES, item["path"]).read_bytes()
        expected_hash, expected_size = fixture_expected_content(item)
        if len(content) != expected_size or digest(content) != expected_hash:
            raise ValueError("Host fixture changed: " + item["path"])
    return len(records)


class PreservationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((ROOT / "integration/import.json").read_text())
        cls.records = {item["target"]: item for item in cls.manifest["files"]}
        cls.format_manifest = json.loads((ROOT / "integration/formatting.json").read_text())
        if cls.format_manifest.get("schema_version") != 1:
            raise ValueError("Unsupported formatting manifest schema")
        cls.formatting = cls.format_manifest["files"]
        if not isinstance(cls.formatting, dict):
            raise ValueError("Invalid formatting inventory")

    def test_imports_preserved_except_explicit_rebases(self):
        self.assertEqual(len(self.records), len(self.manifest["files"]), "Duplicate import target")
        self.assertGreater(len(self.records), 1000, "Unexpectedly incomplete import inventory")
        failures, rebased = [], []
        normalized_count, formatted_count = 0, 0
        for target, item in self.records.items():
            with self.subTest(target=target):
                path = contained(ROOT, target)
                if not path.is_file():
                    failures.append(target + " (missing)")
                    continue
                actual = path.read_bytes()
                actual_hash = digest(actual)
                expected_normalized = item.get("normalized_local_sha256", "")
                if not re.fullmatch(r"[0-9a-f]{64}", expected_normalized):
                    failures.append(target + " (normalized source hash missing or invalid)")
                    continue
                reviewed = item.get("reviewed_target_sha256")
                formatted = formatting_chain_matches(item, actual_hash, self.formatting.get(target))
                if reviewed is not None:
                    if target not in REVIEWED_MERGES or (actual_hash != reviewed and not formatted):
                        failures.append(target + " (reviewed output changed or unlisted)")
                    else:
                        rebased.append(target)
                        formatted_count += int(formatted)
                    continue
                if formatted:
                    formatted_count += 1
                    continue
                if actual_hash == item["local_sha256"]:
                    if digest(normalized(actual)) != expected_normalized:
                        failures.append(target + " (normalized source hash inconsistent)")
                    continue
                if target in REVIEWED_MERGES:
                    failures.append(target + " (reviewed output hash missing)")
                    continue
                if item["action"] == "upstream_equivalent":
                    if digest(normalized(actual)) == expected_normalized:
                        normalized_count += 1
                        continue
                failures.append(target + " (unexpected content change)")
        self.assertFalse(failures, "\n".join(failures[:40]))
        print(f"Import preservation: {len(self.records)} files; {normalized_count} CRLF-only; "
              f"{len(rebased)} separately hash-pinned integration rebases; "
              f"{formatted_count} explicit formatting chains.")

    def test_formatting_provenance(self):
        for target, proof in self.formatting.items():
            with self.subTest(target=target):
                self.assertIsInstance(proof, dict)
                self.assertTrue(valid_hash(proof.get("before_sha256")), target)
                self.assertTrue(valid_hash(proof.get("after_sha256")), target)
                self.assertIsInstance(proof.get("formatter"), str)
                self.assertTrue(proof["formatter"].strip(), target)
                actual_hash = digest(contained(ROOT, target).read_bytes())
                self.assertEqual(actual_hash, proof["after_sha256"], target)
                item = self.records.get(target)
                if item is not None:
                    self.assertTrue(formatting_chain_matches(item, actual_hash, proof),
                                    target + " (formatter input is not the sealed source)")

    def test_fixture_provenance(self):
        self.assertEqual(verify_fixture_inventory(), 15)

    def test_critical_paths_are_manifest_pinned(self):
        required = [
            "firmware_c5/components/Drivers/spi_slave/spi_slave_driver.c",
            "firmware_p4/components/Drivers/spi_bridge_phy/spi_bridge_phy.c",
            "firmware_c5/components/Service/spi_bridge/spi_bridge.c",
            "firmware_p4/components/Service/spi_bridge/spi_bridge.c",
            "firmware_c5/components/Service/spi_bridge/hb_muse_c5_service.c",
            "firmware_c5/components/Service/spi_bridge/session_manager.c",
            "firmware_c5/components/Service/wifi/hb_muse_wifi_guard.inc",
            "firmware_c5/components/Service/wifi/hb_muse_wifi_scan.inc",
            "firmware_c5/components/Service/bluetooth/bluetooth_service.c",
            "firmware_c5/components/Service/highboy_muse/pairing/hb_muse_pairing.c",
            "firmware_c5/components/Service/highboy_muse/pairing/hb_muse_setup.c",
            "firmware_c5/components/Service/highboy_muse/runtime/hb_muse_runtime.c",
            "firmware_c5/components/Service/highboy_muse/bridge/hb_muse_rpc.c",
            "firmware_p4/components/Service/highboy_muse/p4_app/hb_muse_app.c",
        ]
        for target in required:
            with self.subTest(target=target):
                self.assertIn(target, self.records, "Critical source missing from provenance")
                item = self.records[target]
                actual_hash = digest((ROOT / target).read_bytes())
                expected = item.get("reviewed_target_sha256", item["local_sha256"])
                self.assertTrue(actual_hash == expected or formatting_chain_matches(
                    item, actual_hash, self.formatting.get(target)), target)

    def test_protocol_and_dma_contract(self):
        relative = "components/Service/spi_bridge/include/spi_protocol.h"
        left = (ROOT / "firmware_p4" / relative).read_bytes()
        right = (ROOT / "firmware_c5" / relative).read_bytes()
        self.assertTrue(left == right, "P4/C5 SPI headers differ")
        self.assertTrue(re.search(rb"#define\s+SPI_PROTOCOL_VERSION\s+8\b", left))
        for marker in (b"SPI_CAT_FLY = 0x0F", b"SPI_CAT_GROK = 0x10",
                       b"SPI_ID_BT_HID_SEND_MOUSE", b"SPI_WIFI_SPECTRUM_BIN_COUNT 39"):
            self.assertTrue(marker in left, "Local protocol extension removed")
        bridge = (ROOT / "firmware_c5/components/Service/spi_bridge/spi_bridge.c").read_text()
        for marker in ("static DMA_ATTR uint8_t rx_buf", "static DMA_ATTR uint8_t tx_buf",
                       "static spi_slave_transaction_t rx_trans", "case 0x11:",
                       "spi_slave_driver_queue_response_and_receive", "bridge_transport_fault"):
            self.assertTrue(marker in bridge, "C5 transport preservation marker missing")
        phy = (ROOT / "firmware_c5/components/Drivers/spi_slave/spi_slave_driver.c").read_text()
        for marker in ("SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE", "esp_ptr_dma_capable",
                       "portENTER_CRITICAL(&s_pair_lock)", "result != expected",
                       "result->trans_len != expected->length"):
            self.assertTrue(marker in phy, "C5 DMA lifetime/validation marker missing")

    def test_no_automatic_nvs_erase_restored(self):
        paths = ["Core/kernel.c", "Service/wifi/wifi_service.c",
                 "Service/bluetooth/bluetooth_service.c", "Service/host_link/host_link_gatt.c",
                 "Service/meshcore/meshcore_gatt.c", "Service/meshtastic/meshtastic_gatt.c"]
        for relative in paths:
            content = (ROOT / "firmware_c5/components" / relative).read_text()
            self.assertFalse(re.search(r"\bnvs_flash_erase\s*\(", content), relative)

    def test_c5_memory_defaults_and_effective_build(self):
        project = ROOT / "firmware_c5"
        configured = config_values([project / "sdkconfig.defaults", project / "sdkconfig.baseline"])
        for key, expected in C5_MEMORY.items():
            if key != "CONFIG_IDF_TARGET":
                self.assertEqual(configured.get(key), expected, key)
        effective = project / "build_highboy/sdkconfig"
        if effective.is_file():
            values = config_values([effective])
            for key, expected in C5_MEMORY.items():
                self.assertEqual(values.get(key), expected, key)
            print("C5 effective generated memory configuration verified (named settings only).")
        else:
            print("C5 effective configuration unavailable; defaults verified, build check pending.")

    def test_upstream_and_third_party_metadata(self):
        self.assertEqual(self.manifest["upstream"], "011ed7da4c750b7777fd6dbad8ce4541304c6898")
        for key in ("credentials_copied", "runtime_assets_copied", "flashed"):
            self.assertIs(self.manifest[key], False, key)
        sdk_license = ROOT / "third_party/muse_sdk/LICENSE"
        self.assertTrue(sdk_license.is_file(), "Muse SDK license missing")
        self.assertEqual(digest(sdk_license.read_bytes()),
                         "0353a8ae950232a70f0379a4b8a10b661b2f3e3a598e9411332fc0d4f98947fa",
                         "Pinned SDK Apache license changed")
        self.assertTrue((ROOT / "third_party/muse_sdk/README.md").is_file(), "SDK provenance missing")
        self.assertTrue((ROOT / "firmware_c5/components/Service/minimp3/LICENSE").is_file())
        self.assertTrue((ROOT / "LICENSE").is_file(), "TentacleOS license missing")


class FormattingProofTests(unittest.TestCase):
    def setUp(self):
        self.original = digest(b"original source")
        self.reviewed = digest(b"reviewed source")
        self.formatted = digest(b"formatted source")
        self.item = {"local_sha256": self.original, "action": "preserve_local_overlay"}
        self.proof = {"before_sha256": self.original, "after_sha256": self.formatted,
                      "formatter": "clang-format test fixture"}

    def test_exact_original_chain(self):
        self.assertTrue(formatting_chain_matches(self.item, self.formatted, self.proof))

    def test_unknown_input_or_changed_output_rejected(self):
        self.proof["before_sha256"] = self.reviewed
        self.assertFalse(formatting_chain_matches(self.item, self.formatted, self.proof))
        self.proof["before_sha256"] = self.original
        self.assertFalse(formatting_chain_matches(self.item, self.reviewed, self.proof))

    def test_review_seal_cannot_be_bypassed(self):
        self.item["reviewed_target_sha256"] = self.reviewed
        self.assertFalse(formatting_chain_matches(self.item, self.formatted, self.proof))
        self.proof["before_sha256"] = self.reviewed
        self.assertTrue(formatting_chain_matches(self.item, self.formatted, self.proof))

    def test_upstream_equivalent_requires_pinned_upstream_bytes(self):
        self.item.update(action="upstream_equivalent", upstream_sha256=self.reviewed,
                         normalized_local_sha256=digest(b"normalized source"))
        self.proof["before_sha256"] = self.reviewed
        self.assertTrue(formatting_chain_matches(self.item, self.formatted, self.proof))
        self.proof["before_sha256"] = self.item["normalized_local_sha256"]
        self.assertFalse(formatting_chain_matches(self.item, self.formatted, self.proof))
        self.item["action"] = "preserve_local_overlay"
        self.proof["before_sha256"] = self.reviewed
        self.assertFalse(formatting_chain_matches(self.item, self.formatted, self.proof))

    def test_invalid_proof_rejected(self):
        self.assertFalse(formatting_chain_matches(self.item, self.formatted, None))
        for key in ("before_sha256", "after_sha256", "formatter"):
            proof = dict(self.proof)
            proof[key] = ""
            self.assertFalse(formatting_chain_matches(self.item, self.formatted, proof))


class FixtureProofTests(unittest.TestCase):
    def setUp(self):
        self.item = {"path": "tests/test_core.c", "sha256": digest(b"original"), "bytes": 8}

    def reviewed(self):
        return dict(self.item, path="p4_app/test_app.c", reviewed_sha256=digest(b"reviewed"),
                    reviewed_bytes=8, review_reason="Explicit bounded fixture regression")

    def test_original_fixture_evidence(self):
        self.assertEqual(fixture_expected_content(self.item), (self.item["sha256"], 8))

    def test_explicit_review_retains_original_hash(self):
        item = self.reviewed()
        self.assertEqual(fixture_expected_content(item), (item["reviewed_sha256"], 8))
        self.assertEqual(item["sha256"], self.item["sha256"])

    def test_unlisted_review_rejected(self):
        item = self.reviewed()
        item["path"] = "tests/test_core.c"
        with self.assertRaises(ValueError):
            fixture_expected_content(item)

    def test_missing_review_fields_rejected(self):
        for key in ("reviewed_sha256", "reviewed_bytes", "review_reason"):
            item = self.reviewed()
            del item[key]
            with self.subTest(key=key), self.assertRaises(ValueError):
                fixture_expected_content(item)
        item = dict(self.item, path="p4_app/test_app.c")
        with self.assertRaises(ValueError):
            fixture_expected_content(item)

    def test_invalid_hash_size_and_reason_rejected(self):
        for key, value in (("sha256", "bad"), ("bytes", -1), ("bytes", True),
                           ("reviewed_sha256", "bad"), ("reviewed_bytes", -1),
                           ("reviewed_bytes", True), ("review_reason", " ")):
            item = self.reviewed()
            item[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                fixture_expected_content(item)


class CanonicalHostTests(unittest.TestCase):
    """Compile canonical production sources with repository-contained fixtures."""

    @classmethod
    def setUpClass(cls):
        if RUN_HOST:
            verify_fixture_inventory()

    def setUp(self):
        if not RUN_HOST:
            self.skipTest("Host tests explicitly disabled")
        self.compiler = shlex.split(os.environ.get("CC", "cc"))
        if not FIXTURES.is_dir() or not self.compiler or not shutil.which(self.compiler[0]):
            self.fail("Offline fixtures and a host C compiler are required; use --skip-host explicitly")

    def run_fixture(self, fixture, sources, includes=(), defines=(), scenarios=False):
        include_dirs = [base / sub for base in (C5, P4) for sub in
                        ("core", "transport", "sdk_adapter", "platform", "runtime",
                         "pairing", "bridge", "p4_app", "sdk")]
        include_dirs += list(includes)
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wconversion",
                 "-Wshadow", "-g", "-O1", "-fsanitize=undefined", "-fno-sanitize-recover=all"]
        with tempfile.TemporaryDirectory(prefix="highboy-preservation-") as temporary:
            directory = Path(temporary)
            # No legacy production directory beside the fixture: quoted includes
            # must resolve to canonical headers, except explicitly supplied fakes.
            test_source = directory / "fixture.c"
            shutil.copyfile(FIXTURES / fixture, test_source)
            binary = directory / "test"
            command = [*self.compiler, *flags, *["-D" + define for define in defines]]
            for include in include_dirs:
                command += ["-I", str(include)]
            command += [*map(str, sources), str(test_source), "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stderr[-6000:])
            listed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(listed.returncode, 0, listed.stderr[-3000:])
            if scenarios:
                cases = listed.stdout.splitlines()
                self.assertTrue(cases and all(re.fullmatch(r"[a-zA-Z0-9_-]+", case) for case in cases))
                for case in cases:
                    result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=15)
                    self.assertEqual(result.returncode, 0, f"{fixture}: {case}: {result.stderr[-2000:]}")
                print(f"Canonical {fixture}: {len(cases)} isolated UBSan scenarios passed.")
            else:
                summary = listed.stdout.splitlines()
                print(f"Canonical {fixture}: {summary[-1] if summary else 'passed'}")

    def test_core_ptt(self):
        self.run_fixture("tests/test_core.c", [C5 / "core/hb_muse_core.c"])

    def test_wire(self):
        self.run_fixture("transport/test_wire.c", [C5 / "transport/hb_muse_wire.c"])

    def test_wire_client(self):
        self.run_fixture("transport/test_client.c", [P4 / "transport/hb_muse_wire.c",
                                                    P4 / "transport/hb_muse_wire_client.c"])

    def test_pcm(self):
        self.run_fixture("sdk_adapter/test_pcm_adapter.c", [C5 / "sdk_adapter/hb_muse_pcm_adapter.c",
                                                           C5 / "core/hb_muse_core.c"])

    def test_pairing_transport(self):
        self.run_fixture("pairing/test_transport.c", [C5 / "pairing/hb_muse_pairing_fragments.c",
                                                     C5 / "pairing/hb_muse_pairing_gatt.c"],
                         includes=[FIXTURES / "pairing/gatt_fakes"])

    def test_pairing(self):
        cjson = ROOT / "firmware_c5/vendor/cjson/cJSON"
        self.run_fixture("pairing/test_pairing.c", [C5 / "pairing/hb_muse_pairing.c", cjson / "cJSON.c"],
                         includes=[cjson], scenarios=True)

    def test_setup(self):
        self.run_fixture("pairing/test_setup.c", [C5 / "pairing/hb_muse_setup.c",
                                                 C5 / "pairing/hb_muse_pairing_fragments.c"],
                         includes=[FIXTURES / "pairing", ROOT / "firmware_c5/vendor/cjson/cJSON"],
                         defines=["HB_MUSE_SETUP_HOST_TEST"], scenarios=True)

    def test_rpc(self):
        self.run_fixture("bridge/test_rpc.c", [C5 / "bridge/hb_muse_rpc.c", C5 / "transport/hb_muse_wire.c"])

    def test_endpoint(self):
        self.run_fixture("transport/test_endpoint.c", [C5 / "transport/hb_muse_wire.c",
                         P4 / "transport/hb_muse_wire_client.c", C5 / "transport/hb_muse_endpoint.c",
                         C5 / "sdk_adapter/hb_muse_pcm_adapter.c", C5 / "sdk_adapter/hb_muse_sdk_binding.c"])

    def test_p4_ptt_app(self):
        self.run_fixture("p4_app/test_app.c", [P4 / "p4_app/hb_muse_app.c", C5 / "core/hb_muse_core.c",
                         C5 / "transport/hb_muse_wire.c", P4 / "transport/hb_muse_wire_client.c",
                         C5 / "transport/hb_muse_endpoint.c", C5 / "sdk_adapter/hb_muse_pcm_adapter.c",
                         C5 / "sdk_adapter/hb_muse_sdk_binding.c"])

    def test_wifi_json_bound(self):
        self.run_fixture("integrated_c5/test_wifi_json_bound.c", [],
                         includes=[ROOT / "firmware_c5/components/Service/wifi"])


def main():
    global RUN_HOST
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-host", action="store_true", help="Run source/config gates only")
    args, remaining = parser.parse_known_args()
    RUN_HOST = not args.skip_host
    unittest.main(argv=[__file__, *remaining], verbosity=2)


if __name__ == "__main__":
    main()
