#!/bin/bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
base="${1:?Expected base SHA}"
head="${2:?Expected head SHA}"
lint="$repo/node_modules/.bin/commitlint"
config="$repo/commitlint.config.js"

git rev-parse --verify "${head}^{commit}" >/dev/null
if [ "$base" = 0000000000000000000000000000000000000000 ]; then
  # A new branch has no previous push range; check its tip without linting legacy history.
  git show -s --format=%B "$head" | "$lint" --config "$config" --verbose
else
  git rev-parse --verify "${base}^{commit}" >/dev/null
  "$lint" --config "$config" --from "$base" --to "$head" --verbose
fi
