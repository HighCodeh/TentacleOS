#!/bin/bash

REPO_ROOT="$(git rev-parse --show-toplevel)"

TARGETS=(
  "$REPO_ROOT/firmware_p4/components"
  "$REPO_ROOT/firmware_p4/main"
  "$REPO_ROOT/firmware_c5/components"
  "$REPO_ROOT/firmware_c5/main"
)

EXCLUDE_DIRS=(
  "managed_components"
  "build"
)

EXCLUDE_ARGS=""
for dir in "${EXCLUDE_DIRS[@]}"; do
  EXCLUDE_ARGS="$EXCLUDE_ARGS -not -path */$dir/*"
done

EXISTING_TARGETS=""
for target in "${TARGETS[@]}"; do
  if [ -d "$target" ]; then
    EXISTING_TARGETS="$EXISTING_TARGETS $target"
  fi
done

if [ -z "$EXISTING_TARGETS" ]; then
  echo "No target directories found."
  exit 0
fi

FILES=$(find $EXISTING_TARGETS \( -name "*.c" -o -name "*.h" \) $EXCLUDE_ARGS)

if [ -z "$FILES" ]; then
  echo "No files to format."
  exit 0
fi

COUNT=$(echo "$FILES" | wc -l)

if [ "$1" = "--check" ]; then
  echo "Checking $COUNT files..."
  echo "$FILES" | xargs clang-format --dry-run --Werror 2>&1
  if [ $? -ne 0 ]; then
    echo "Formatting errors found. Run ./scripts/format.sh to fix."
    exit 1
  fi
  echo "All files formatted correctly."
else
  echo "Formatting $COUNT files..."
  echo "$FILES" | xargs clang-format -i
  echo "Done."
fi
