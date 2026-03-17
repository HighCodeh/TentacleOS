#!/bin/bash

# ==============================================================================
# TentacleOS Developer Environment Setup
# ==============================================================================
# Run this once after cloning the repository to configure git hooks.

PROJECT_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." &> /dev/null && pwd)
HOOKS_SRC="$PROJECT_ROOT/tools/hooks"
HOOKS_DST="$PROJECT_ROOT/.git/hooks"

GREEN='\033[0;32m'
NC='\033[0m'

echo "Setting up TentacleOS development environment..."

if [ -d "$HOOKS_SRC" ]; then
  for hook in "$HOOKS_SRC"/*; do
    hook_name=$(basename "$hook")
    cp "$hook" "$HOOKS_DST/$hook_name"
    chmod +x "$HOOKS_DST/$hook_name"
    echo -e "  Installed git hook: ${GREEN}${hook_name}${NC}"
  done
fi

echo -e "${GREEN}Setup complete!${NC}"
