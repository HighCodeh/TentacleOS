#!/usr/bin/env python3
"""Package checked app binaries with source/link evidence, never a flash command."""
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import check_contracts

ROOT = Path(__file__).resolve().parents[2]
PARTITION_SHA = {
    "p4": "bb76257d010de4925ddf8925b46a2f89e0512a2a858e0c4939d705c74a1e51bd",
    "c5": "236a9c2e19bf6373b94589e0683c3a588523c026a1a21dae141ed2deda97c7a1",
}
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


def main():
    subprocess.run(["bash", str(ROOT / "tools/highboy/build.command")], check=True)
    subprocess.run(["bash", str(ROOT / "tools/highboy/test.command")], check=True)
    check_contracts.main()
    report = {
        "status": "compiled_candidate_requires_hardware_acceptance",
        "upstream": "011ed7da4c750b7777fd6dbad8ce4541304c6898",
        "targets": {}, "flashed": False, "assets_rebuilt": False,
        "runtime_validated": False, "partition_migration": False,
        "host_test_suites": ["preservation_and_muse", "subghz", "usb_msc", "doom_dma"],
        "host_tests_passed": True,
        "speaker_reply_implemented": False,
        "deployment_warning": "No flash authorization in this package. The physical P4 PS1 erase requires a separately reviewed asset/boot restore. C5 must remain protocol8/Muse-compatible.",
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
        missing = set(REQUIRED[chip]) - names
        if missing:
            raise SystemExit(chip + " missing linked preservation symbols: " + str(sorted(missing)))
        if chip == "p4" and {"ui_grok_bot_open", "grok_client_start", "hb_muse_setup_set_local_token"} & names:
            raise SystemExit("Unexpected cloud Grok or C5 credential entrypoint on P4")
        if chip == "p4" and "protocol_rossi" in names:
            raise SystemExit("Unvalidated Rossi stub must not be linked into the registry")
        inputs = []
        for parent in (project / "components", project / "main"):
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
            "linked_preservation_symbols": list(REQUIRED[chip]),
        }
    name = "candidate_" + "_".join(report["targets"][c]["sha256"][:12] for c in ("P4", "C5"))
    dest = ROOT / "integration/release" / name
    dest.mkdir(parents=True, exist_ok=True)
    for chip in ("p4", "c5"):
        meta = report["targets"][chip.upper()]
        src = ROOT / ("firmware_" + chip) / "build_highboy" / meta["file"]
        out = dest / meta["file"]
        if out.exists() and digest(out) != meta["sha256"]:
            raise SystemExit("Existing release content differs")
        shutil.copyfile(src, out)
    (dest / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    print("Package:", dest)


if __name__ == "__main__":
    main()
