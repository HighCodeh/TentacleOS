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

# Build the integrated application pair only. Never access a device or API.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
explicit_names=()
explicit_values=()
for variable in HIGHBOY_P4_IDF_PATH HIGHBOY_P4_TOOLS_PATH HIGHBOY_P4_PYTHON_ENV \
  HIGHBOY_C5_IDF_PATH HIGHBOY_C5_TOOLS_PATH HIGHBOY_C5_PYTHON_ENV; do
  if printenv "$variable" >/dev/null; then
    explicit_names+=("$variable")
    explicit_values+=("${!variable}")
  fi
done
local_env="${HIGHBOY_BUILD_ENV:-$repo/.highboy-build.env}"
if [ -f "$local_env" ]; then
  # Optional, untracked local toolchain paths; never store account credentials here.
  source "$local_env"
elif [ -n "${HIGHBOY_BUILD_ENV:-}" ]; then
  echo "Requested local build environment does not exist: $local_env" >&2
  exit 2
fi
for ((index=0; index<${#explicit_names[@]}; index++)); do
  export "${explicit_names[$index]}=${explicit_values[$index]}"
done
target="${1:-all}"
if [ "$#" -gt 1 ]; then exit 2; fi
case "$target" in
  all)
    bash "$0" c5
    bash "$0" p4
    exit
    ;;
  p4)
    idf_override=HIGHBOY_P4_IDF_PATH
    selected_idf="${HIGHBOY_P4_IDF_PATH:-${IDF_PATH:-}}"
    selected_tools="${HIGHBOY_P4_TOOLS_PATH:-}"
    selected_python="${HIGHBOY_P4_PYTHON_ENV:-}"
    expected_tag=v6.1
    overlay=
    ;;
  c5)
    idf_override=HIGHBOY_C5_IDF_PATH
    selected_idf="${HIGHBOY_C5_IDF_PATH:-${IDF_PATH:-}}"
    selected_tools="${HIGHBOY_C5_TOOLS_PATH:-}"
    selected_python="${HIGHBOY_C5_PYTHON_ENV:-}"
    expected_tag=v6.1
    overlay=sdkconfig.baseline
    ;;
  *) echo 'Usage: build.command [all|p4|c5]' >&2; exit 2 ;;
esac
if [ -z "$selected_idf" ] || [ ! -f "$selected_idf/export.sh" ]; then
  echo "Set $idf_override or activate ESP-IDF $expected_tag before building $target." >&2
  exit 2
fi
export IDF_PATH="$selected_idf"
head_revision="$(git -C "$IDF_PATH" rev-parse HEAD)"
tag_revision="$(git -C "$IDF_PATH" rev-parse "$expected_tag^{commit}")"
if [ "$head_revision" != "$tag_revision" ]; then
  echo "Unexpected IDF revision for $target; expected $expected_tag." >&2
  exit 2
fi
if [ -n "$selected_tools" ]; then export IDF_TOOLS_PATH="$selected_tools"; fi
if [ -n "$selected_python" ]; then
  export IDF_PYTHON_ENV_PATH="$selected_python"
  export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
fi
unset SDKCONFIG SDKCONFIG_DEFAULTS IDF_TARGET EXTRA_COMPONENT_DIRS
export IDF_COMPONENT_MANAGER=1 PIP_NO_INDEX=1 PYTHONDONTWRITEBYTECODE=1 CCACHE_DISABLE=1
if [ "$target" = c5 ]; then
  git -C "$IDF_PATH" submodule status -- \
    components/esp_wifi/lib components/esp_phy/lib components/esp_coex/lib \
    components/bt/controller/lib_esp32c5/esp32c5-bt-lib \
    components/bt/host/nimble/nimble components/lwip/lwip \
    components/mbedtls/mbedtls components/heap/tlsf \
    components/bootloader/subproject/components/micro-ecc/micro-ecc \
    components/spiffs/spiffs components/unity/unity components/cmock/CMock \
    components/protobuf-c/protobuf-c | \
    awk 'substr($0,1,1)!=" " {bad=1} END {if(NR!=13 || bad) exit 1}'
else
  # P4 uses the C5 radio over SPI; unrelated radio libraries are not build inputs.
  git -C "$IDF_PATH" submodule status -- \
    components/lwip/lwip components/mbedtls/mbedtls components/heap/tlsf \
    components/bootloader/subproject/components/micro-ecc/micro-ecc \
    components/spiffs/spiffs components/unity/unity components/cmock/CMock \
    components/protobuf-c/protobuf-c | \
    awk 'substr($0,1,1)!=" " {bad=1} END {if(NR!=8 || bad) exit 1}'
fi
export IDF_SKIP_CHECK_SUBMODULES=1
activation="${IDF_TOOLS_PATH:-}/activate_idf_${expected_tag}.sh"
if [ -f "$activation" ]; then
  eim_environment="$(bash "$activation" -e)"
  while IFS='=' read -r name value; do
    case "$name" in
      PATH) export PATH="$value:$PATH" ;;
      IDF_*|ESP_*|OPENOCD_SCRIPTS) export "$name=$value" ;;
    esac
  done <<< "$eim_environment"
else
  source "$IDF_PATH/export.sh" >/dev/null
fi
export IDF_TARGET="esp32$target"
project="$repo/firmware_$target"
build="$project/build_highboy"
export SDKCONFIG_DEFAULTS="$project/sdkconfig.defaults"
if [ -n "$overlay" ]; then
  SDKCONFIG_DEFAULTS+=";$project/$overlay"
fi
python "$repo/tools/highboy/check_contracts.py"
configure_args=(-D "HIGH_BOY_USB_VOICE=OFF")
if [ "$target" = p4 ]; then
  configure_args+=( -D "TOS_APP_PRODUCTION=${TOS_APP_PRODUCTION:-OFF}"
                -D "TOS_APP_TRUST_PUBLIC_KEY=${TOS_APP_TRUST_PUBLIC_KEY:-}" )
fi
python "$IDF_PATH/tools/idf.py" -C "$project" -B "$build" \
  -D "SDKCONFIG=$build/sdkconfig" -D "SDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS" \
  "${configure_args[@]}" reconfigure
python "$repo/tools/highboy/check_contracts.py" --require-built "$target"
cmake --build "$build" --target app --parallel "${HIGHBOY_BUILD_JOBS:-2}"
python "$repo/tools/highboy/check_contracts.py" --require-built "$target"
echo "$target application built. No assets, flash, hardware, microphone or cloud session accessed."
