#!/bin/bash
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

# Uses the pinned LVGL already resolved by the P4 build; all inputs are host fixtures.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "$#" -gt 1 ]; then
  echo 'Usage: test_ui.command [host-build-directory]' >&2
  exit 2
fi
if [ ! -f "$repo/firmware_p4/managed_components/lvgl__lvgl/CMakeLists.txt" ]; then
  echo 'Build P4 first to resolve its pinned LVGL dependency.' >&2
  exit 2
fi
ui_test_dir="${1:-$(mktemp -d "${TMPDIR:-/tmp}/highboy-ui.XXXXXX")}"
mkdir -p "$ui_test_dir"
python3 "$repo/tools/highboy/tests/test_keyboard_ui.py" "$ui_test_dir"
python3 "$repo/tools/highboy/tests/test_ux_flows.py" "$ui_test_dir"
python3 "$repo/tools/highboy/tests/test_home_ui.py" "$ui_test_dir"
python3 "$repo/tools/highboy/tests/test_battery_layout.py" "$ui_test_dir"
python3 "$repo/tools/highboy/tests/test_nfc_guidance_ui.py" "$ui_test_dir"
printf 'UI host evidence: %s\n' "$ui_test_dir"
