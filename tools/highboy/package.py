#!/usr/bin/env python3
"""Package checked app binaries with source/link evidence, never a flash command."""
import hashlib
import re
import tempfile
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import check_contracts
import verify_package

ROOT = Path(__file__).resolve().parents[2]
PARTITION_SHA = {chip.lower(): profile['partition_table_sha256']
                 for chip, profile in verify_package.PROFILE.items()}
REQUIRED = {
    "p4": (
        "app_main", "kernel_init", "ui_muse_open", "hb_muse_p4_key",
        "ui_grok_idle_open", "ui_games_menu_open", "D_DoomMain", "gb_init",
        "fly_local3d_render", "nfc_apdu_lab_start", "nfc_listener_start_emu",
        "ui_nfc_write_open", "st25r3916_core_init", "lf_rfid_read_em4100",
        "tud_hid_n_mouse_report", "tud_hid_n_keyboard_report",
        "tinyusb_msc_new_storage_sdmmc", "ui_usb_storage_open", "f_mount",
        "cc1101_init", "sx1262_init", "ui_ir_menu_open", "ui_wifi_scan_open",
        "ui_ble_scan_open", "ui_settings_open", "power_policy_set_display_hold",
        "subghz_protocol_registry_selftest", "protocol_gatetx", "protocol_holtek_ht12x",
        "protocol_keeloq",
    ),
    "c5": ("app_main", "hb_muse_endpoint_process", "hb_muse_setup_open",
           "hb_muse_runtime_init", "muse_hatch_start", "wifi_service_init",
           "spi_bridge_slave_init_mode", "hb_muse_c5_service_execute",
           "hb_muse_pairing_gatt_register", "hb_muse_token_uart_arm",
           "hb_muse_wifi_provision_guarded", "hb_muse_wifi_scan_guarded"),
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def linked_symbols(names, required):
    resolved = {}
    for name in required:
        matches = sorted(symbol for symbol in names if symbol == name or
                         re.fullmatch(re.escape(name) + r"(?:\.(?:constprop|isra|part|lto_priv)\.[0-9]+)+", symbol))
        if matches:
            resolved[name] = {"kind": "symbol", "linked": matches}
        elif name == "subghz_protocol_registry_selftest" and "cmd_subghz" in names:
            resolved[name] = {"kind": "inlined_console_selftest", "linked": ["cmd_subghz"],
                              "host_test": "tools/highboy/test_subghz.py"}
        else:
            raise ValueError("Missing linked preservation symbol: " + name)
    return resolved


def app_signing_configuration(project, build):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if ":" in line and "=" in line and not line.startswith(("#", "//")):
            key, value = line.split("=", 1)
            cache[key.split(":", 1)[0]] = value
    production = cache.get("TOS_APP_PRODUCTION") == "ON"
    custom = bool(cache.get("TOS_APP_TRUST_PUBLIC_KEY"))
    if production and not custom:
        raise ValueError("Production app-signing configuration has no selected key")
    header = build / "esp-idf/Service/generated/tos_build_trust_keys.h" if custom else project / "components/Service/app_runtime/include/tos_trust_keys.h"
    return {"production_mode": production, "custom_public_key": custom,
            "key_custody_verified": False, "trust_store_sha256": digest(header)}


def write_package(report, images, destination):
    verify_package.validate_report(report)
    verify_package.require(set(images) == set(verify_package.PROFILE), 'Missing or unexpected image inputs')
    payload = (json.dumps(report, indent=2) + "\n").encode()
    identity = hashlib.sha256(payload).hexdigest()[:12]
    name = "candidate_" + "_".join(report["targets"][chip]["sha256"][:12]
                                  for chip in ("P4", "C5")) + "_" + identity
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    final = destination / name
    if final.exists():
        verify_package.verify(final)
        if (final / "manifest.json").read_bytes() != payload:
            raise ValueError("Existing candidate manifest differs")
        return final
    with tempfile.TemporaryDirectory(prefix=".candidate-", dir=destination) as temporary:
        staging = Path(temporary) / "package"
        staging.mkdir()
        for chip, source in images.items():
            shutil.copyfile(source, staging / report["targets"][chip]["file"])
        (staging / "manifest.json").write_bytes(payload)
        verify_package.verify(staging)
        staging.rename(final)
    return final


def main():
    subprocess.run(["bash", str(ROOT / "tools/highboy/build.command")], check=True)
    subprocess.run(["bash", str(ROOT / "tools/highboy/test.command")], check=True)
    check_contracts.main()
    images = {}
    report = {
        "app_signing": app_signing_configuration(ROOT / "firmware_p4", ROOT / "firmware_p4/build_highboy"),
        "schema_version": 1, "protocol_version": 8, "app_only": True,
        "status": "compiled_candidate_requires_hardware_acceptance",
        "upstream": "011ed7da4c750b7777fd6dbad8ce4541304c6898",
        "targets": {}, "flashed": False, "assets_rebuilt": False,
        "runtime_validated": False, "partition_migration": False,
        "host_test_runner_sha256": digest(ROOT / "tools/highboy/test.command"),
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
        "host_tests_passed": True,
        "deployment_warning": "App-only candidate for matching existing installations. It contains no bootloader/assets, production provisioning or full restoration. Both chips must remain protocol8/Muse-compatible.",
    }
    for chip in ("p4", "c5"):
        project = ROOT / ("firmware_" + chip)
        build = project / "build_highboy"
        app = build / ("TentacleOS_" + chip.upper() + ".bin")
        elf = app.with_suffix(".elf")
        table = build / "partition_table/partition-table.bin"
        if digest(table) != PARTITION_SHA[chip]:
            raise SystemExit("Unexpected partition binary for " + chip)
        slot = 0x2d0000 if chip == "p4" else 0x200000
        if app.stat().st_size > slot:
            raise SystemExit(chip + " exceeds the preserved app slot")
        commands = json.loads((build / "compile_commands.json").read_text())
        compiler = Path(shlex.split(commands[0]["command"])[0])
        nm = compiler.with_name("riscv32-esp-elf-nm")
        symbols = subprocess.check_output([str(nm), "--defined-only", str(elf)], text=True)
        names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
        symbols = linked_symbols(names, REQUIRED[chip])
        if chip == "p4" and {"ui_grok_bot_open", "grok_client_start", "hb_muse_setup_set_local_token"} & names:
            raise SystemExit("Unexpected cloud Grok or C5 credential entrypoint on P4")
        if chip == "p4" and "protocol_rossi" in names:
            raise SystemExit("Unvalidated Rossi stub must not be linked into the registry")
        inputs = []
        for parent in (project / "components", project / "main", ROOT / "common/metadata"):
            for path in sorted(parent.rglob("*")):
                if path.is_file():
                    inputs.append((str(path.relative_to(ROOT)), digest(path)))
        report["targets"][chip.upper()] = {
            "file": app.name, "sha256": digest(app), "bytes": app.stat().st_size,
            "slot_bytes": slot, "free_bytes": slot - app.stat().st_size,
            "elf_sha256": digest(elf), "sdkconfig_sha256": digest(build / "sdkconfig"),
            "partition_csv_sha256": digest(project / "partitions.csv"),
            "partition_table_sha256": digest(table),
            "source_sha256": hashlib.sha256(json.dumps(inputs).encode()).hexdigest(),
            "linked_preservation_symbols": symbols,
            "idf_version": verify_package.PROFILE[chip.upper()]["idf_version"],
            "compile_commands_sha256": digest(build / "compile_commands.json"),
        }
        images[chip.upper()] = app
    dest = write_package(report, images, ROOT / "integration/release")
    print(json.dumps(report, indent=2))
    print("Package:", dest)


if __name__ == "__main__":
    main()
