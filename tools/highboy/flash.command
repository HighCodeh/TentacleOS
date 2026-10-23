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

set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
case "${1:-}" in
  -h|--help) exec python3 "$repo/tools/highboy/flash.py" --help ;;
esac
target=p4
case "${1:-}" in p4|c5) target="$1"; shift ;; esac
if [ "$target" = c5 ]; then
  idf_variable=HIGHBOY_C5_IDF_PATH
  python_variable=HIGHBOY_C5_PYTHON_ENV
else
  idf_variable=HIGHBOY_P4_IDF_PATH
  python_variable=HIGHBOY_P4_PYTHON_ENV
fi
selected_idf="${!idf_variable:-}"
selected_python="${!python_variable:-}"
local_env="${HIGHBOY_BUILD_ENV:-$repo/.highboy-build.env}"
if [ -f "$local_env" ]; then source "$local_env"; fi
selected_idf="${selected_idf:-${!idf_variable:-${IDF_PATH:-}}}"
selected_python="${selected_python:-${!python_variable:-${IDF_PYTHON_ENV_PATH:-}}}"
if [ -z "$selected_idf" ] || [ ! -f "$selected_idf/components/partition_table/gen_esp32part.py" ]; then
  echo "Set $idf_variable or activate the $target ESP-IDF environment." >&2
  exit 2
fi
python_bin=python3
if [ -n "$selected_python" ]; then python_bin="$selected_python/bin/python"; fi
exec "$python_bin" "$repo/tools/highboy/flash.py" --idf-path "$selected_idf" --target "$target" "$@"
