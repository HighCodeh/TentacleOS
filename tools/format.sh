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

set -o pipefail

usage() {
  echo "Usage: $0 [--check | --changed <base-ref> | --check-changed <base-ref>]"
}

MODE="format"
BASE_REF=""
case "${1:-}" in
  "")
    if [ "$#" -ne 0 ]; then
      usage
      exit 2
    fi
    ;;
  --check)
    if [ "$#" -ne 1 ]; then
      usage
      exit 2
    fi
    MODE="check"
    ;;
  --changed)
    if [ "$#" -ne 2 ] || [ -z "$2" ]; then
      usage
      exit 2
    fi
    BASE_REF="$2"
    ;;
  --check-changed)
    if [ "$#" -ne 2 ] || [ -z "$2" ]; then
      usage
      exit 2
    fi
    MODE="check"
    BASE_REF="$2"
    ;;
  *)
    usage
    exit 2
    ;;
esac

REPO_ROOT="$(git rev-parse --show-toplevel)"

TARGETS=(
  "$REPO_ROOT/firmware_p4/components"
  "$REPO_ROOT/firmware_p4/main"
  "$REPO_ROOT/firmware_c5/components"
  "$REPO_ROOT/firmware_c5/main"
)

DIFF_TARGETS=(
  "firmware_p4/components"
  "firmware_p4/main"
  "firmware_c5/components"
  "firmware_c5/main"
)

FILES=()
if [ -n "$BASE_REF" ]; then
  if ! git -C "$REPO_ROOT" rev-parse --verify "${BASE_REF}^{commit}" >/dev/null 2>&1; then
    echo "Unknown base ref: $BASE_REF"
    exit 2
  fi

  while IFS= read -r -d '' path; do
    case "$path" in
      *.c|*.h) FILES+=("$REPO_ROOT/$path") ;;
    esac
  done < <(git -C "$REPO_ROOT" diff --name-only -z --diff-filter=ACMR \
    "$BASE_REF"...HEAD -- "${DIFF_TARGETS[@]}")
else
  EXISTING_TARGETS=()
  for target in "${TARGETS[@]}"; do
    if [ -d "$target" ]; then
      EXISTING_TARGETS+=("$target")
    fi
  done

  if [ "${#EXISTING_TARGETS[@]}" -eq 0 ]; then
    echo "No target directories found."
    exit 0
  fi

  while IFS= read -r -d '' path; do
    FILES+=("$path")
  done < <(find "${EXISTING_TARGETS[@]}" -type f \( -name "*.c" -o -name "*.h" \) \
    -not -path "*/managed_components/*" -not -path "*/build/*" -print0)
fi

COUNT="${#FILES[@]}"
if [ "$COUNT" -eq 0 ]; then
  echo "No files to format."
  exit 0
fi

if [ "$MODE" = "check" ]; then
  echo "Checking $COUNT files..."
  if ! printf '%s\0' "${FILES[@]}" | xargs -0 clang-format --dry-run --Werror; then
    if [ -n "$BASE_REF" ]; then
      echo "Formatting errors found. Run ./tools/format.sh --changed \"$BASE_REF\" to fix."
    else
      echo "Formatting errors found. Run ./tools/format.sh to fix."
    fi
    exit 1
  fi
  echo "All files formatted correctly."
else
  echo "Formatting $COUNT files..."
  printf '%s\0' "${FILES[@]}" | xargs -0 clang-format -i
  echo "Done."
fi
