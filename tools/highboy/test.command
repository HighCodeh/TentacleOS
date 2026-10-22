#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
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

# Public-source host tests only; no device or account access.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
python3 "$repo/tools/highboy/check_contracts.py"
python3 "$repo/tools/highboy/test_contracts.py"
python3 "$repo/tools/highboy/test_package.py"
python3 "$repo/tools/highboy/test_release.py"
python3 "$repo/tools/highboy/test_security.py"
python3 "$repo/tools/highboy/test_recovery.py"
python3 "$repo/tools/ota/test_ota.py"
python3 "$repo/tools/ota/test_update_ui.py"
python3 "$repo/tools/boot_report/test_boot_report.py"
python3 "$repo/tools/power_policy/test_power_policy.py"
python3 "$repo/tools/app_sdk/test_trust_config.py"
python3 "$repo/tools/app_sdk/test_signed_bundle.py"
python3 "$repo/tools/app_sdk/test_build.py"
python3 "$repo/tools/app_sdk/test_rf_api.py"
python3 "$repo/tools/app_sdk/test_radio_view.py"
python3 "$repo/tools/lora_nodes/test_nodes.py"
python3 "$repo/tools/subghz_api/test_subghz_api.py"
python3 "$repo/tools/highboy/test_subghz.py"
python3 "$repo/tools/highboy/test_subghz_lifecycle.py"
python3 "$repo/tools/highboy/test_lora_airtime.py"
python3 "$repo/tools/highboy/test_lora_transport.py"
python3 "$repo/tools/highboy/test_rf_ownership.py"
python3 "$repo/tools/mqtt/test_mqtt.py"
python3 "$repo/tools/gatt/test_gatt.py"
python3 "$repo/tools/host_link/test_pairing.py"
python3 "$repo/tools/host_link/test_rf.py"
python3 "$repo/tools/host_link/test_rf_cli.py"
python3 "$repo/tools/highboy/test_nfc_screen_owners.py"
python3 "$repo/tools/highboy/test_ir_library.py"
python3 "$repo/tools/ir_api/test_ir_api.py"
python3 "$repo/tools/scripts/generate_js_stdlib.py" --check
python3 "$repo/tools/scripts/test_engine.py"
python3 "$repo/tools/scripts/test_runner.py"
python3 "$repo/tools/scripts/test_context.py"
python3 "$repo/tools/scripts/test_ui.py"
python3 "$repo/tools/highboy/test_muse_native.py"
python3 "$repo/tools/highboy/test_preservation.py"
node --test "$repo/tools/highboy/tests/test_usb_msc.mjs" \
  "$repo/tools/highboy/tests/test_doom_dma.mjs"
