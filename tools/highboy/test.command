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
test_failures=0
run_test() {
  if "$@"; then
    return 0
  fi
  printf 'FAILED host test group: %s\n' "$*" >&2
  test_failures=$((test_failures + 1))
}
run_test python3 "$repo/tools/ci/test_host_test_runner.py"
run_test python3 "$repo/tools/highboy/check_contracts.py"
run_test python3 "$repo/tools/wifi_quick_scan/test_quick_scan.py"
run_test python3 "$repo/tools/highboy/test_contracts.py"
run_test python3 "$repo/tools/highboy/test_package.py"
run_test python3 "$repo/tools/highboy/test_release.py"
run_test python3 "$repo/tools/highboy/test_security.py"
run_test python3 "$repo/tools/highboy/test_recovery.py"
run_test python3 "$repo/tools/ota/test_ota.py"
run_test python3 "$repo/tools/ota/test_update_ui.py"
run_test python3 "$repo/tools/boot_report/test_boot_report.py"
run_test python3 "$repo/tools/power_policy/test_power_policy.py"
run_test python3 "$repo/tools/app_sdk/test_trust_config.py"
run_test python3 "$repo/tools/app_sdk/test_signed_bundle.py"
run_test python3 "$repo/tools/app_sdk/test_build.py"
run_test python3 "$repo/tools/app_sdk/test_rf_api.py"
run_test python3 "$repo/tools/app_sdk/test_radio_view.py"
run_test python3 "$repo/tools/lora_nodes/test_nodes.py"
run_test python3 "$repo/tools/subghz_api/test_subghz_api.py"
run_test python3 "$repo/tools/highboy/test_subghz.py"
run_test python3 "$repo/tools/highboy/test_subghz_lifecycle.py"
run_test python3 "$repo/tools/highboy/test_lora_airtime.py"
run_test python3 "$repo/tools/highboy/test_lora_transport.py"
run_test python3 "$repo/tools/highboy/test_rf_ownership.py"
run_test python3 "$repo/tools/meshcore/test_key_agreement.py"
run_test python3 "$repo/tools/mqtt/test_mqtt.py"
run_test python3 "$repo/tools/gatt/test_gatt.py"
run_test python3 "$repo/tools/host_link/test_pairing.py"
run_test python3 "$repo/tools/host_link/test_rf.py"
run_test python3 "$repo/tools/host_link/test_rf_cli.py"
run_test python3 "$repo/tools/nfc/test_legacy_des.py"
run_test python3 "$repo/tools/highboy/test_nfc_screen_owners.py"
run_test python3 "$repo/tools/highboy/test_ir_library.py"
run_test python3 "$repo/tools/ir_api/test_ir_api.py"
run_test python3 "$repo/tools/scripts/generate_js_stdlib.py" --check
run_test python3 "$repo/tools/scripts/test_engine.py"
run_test python3 "$repo/tools/scripts/test_runner.py"
run_test python3 "$repo/tools/scripts/test_context.py"
run_test python3 "$repo/tools/scripts/test_ui.py"
run_test python3 "$repo/tools/highboy/test_muse_native.py"
run_test python3 "$repo/tools/highboy/test_preservation.py"
run_test node --test "$repo/tools/highboy/tests/test_usb_msc.mjs" \
  "$repo/tools/highboy/tests/test_doom_dma.mjs"
if [ "$test_failures" -ne 0 ]; then
  printf '%s host test group(s) failed.\n' "$test_failures" >&2
  exit 1
fi
