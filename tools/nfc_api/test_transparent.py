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

"""Check production transparent modulation timing and shared-bus cleanup."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from test_emu_tx import extract, ROOT

source = (ROOT / "firmware_p4/components/Drivers/st25r3916/hal/hb_nfc_spi.c").read_text()
functions = "\n".join(extract(source, name) for name in (
    "static bool receive_done(", "esp_err_t hb_nfc_spi_capture_gaps(",
    "esp_err_t hb_nfc_spi_prepare_target_tx(", "static uint16_t target_ticks(", "static void encode_target_bit(",
    "static esp_err_t transmit_target_encoded(rmt_encoder_handle_t encoder, const void *data,\n                                         size_t size, int timeout_ms) {",
    "static esp_err_t transmit_target(size_t count) {",
    "esp_err_t hb_nfc_spi_target_tx_bits(",
    "esp_err_t hb_nfc_spi_target_pulses(",
    "static size_t encode_v(",
    "esp_err_t hb_nfc_spi_target_v(",
    "esp_err_t hb_nfc_spi_target_v_exchange(",
))
constants = "\n".join(re.findall(r"^#define HB_NFC_[^\n]+", source, re.M))
fixture = Path(__file__).with_suffix(".c").read_text()
fixture = fixture.replace("/* CONSTANTS */", constants).replace("/* PRODUCTION */", functions)
with tempfile.TemporaryDirectory(prefix="nfc-transparent-") as tmp:
    directory = Path(tmp)
    (directory / "test.c").write_text(fixture)
    (directory / "esp_err.h").write_text("typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n")
    (directory / "esp_log.h").write_text("#define ESP_LOGE(tag, ...) ((void)(tag))\n")
    hal = ROOT / "firmware_p4/components/Drivers/st25r3916/hal"
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-I" + str(directory), "-I" + str(hal / "include"),
                    str(hal / "hb_nfc_v_codec.c"), str(directory / "test.c"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
