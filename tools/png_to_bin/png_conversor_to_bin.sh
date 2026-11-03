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


# ==============================================================================
# LVGL Image Conversion Automation Script (Linux/macOS Version)
# ==============================================================================

set -euo pipefail

# Cores para o terminal
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Definição de Caminhos
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
VENV_DIR="$SCRIPT_DIR/lvgl-env"
PYTHON_BIN="$VENV_DIR/bin/python3"
LVGL_IMAGE_PY="$SCRIPT_DIR/LVGLImage.py"
SCRIPT_URL="https://raw.githubusercontent.com/lvgl/lvgl/v9.4.0/scripts/LVGLImage.py"

# Caminhos de trabalho (baseados no diretório de execução do CMake)
WORKING_DIR=$(pwd)
ASSETS_DIR="$WORKING_DIR/assets"
TEMP_DIR="${HIGHBOY_ASSETS_OUTPUT:-$WORKING_DIR/temp}"

echo -e "\n${YELLOW}>>> Starting LVGL Assets Automation (Linux/Mac)${NC}"

if [ ! -f "$PYTHON_BIN" ]; then
  echo -e "${YELLOW}Virtual environment 'lvgl-env' not found in $SCRIPT_DIR. Creating...${NC}"
  rm -rf "$VENV_DIR"
  "${HIGHBOY_ASSETS_PYTHON:-python3}" -m venv "$VENV_DIR"

  if [ $? -ne 0 ]; then
    echo -e "${RED}Critical Error: Failed to create Python virtual environment.${NC}"
    echo "Make sure python3-venv is installed (sudo apt install python3-venv)"
    exit 1
  fi

  # Resolve the actual python binary name inside the venv
  if [ ! -f "$PYTHON_BIN" ]; then
    if [ -f "$VENV_DIR/bin/python" ]; then
      PYTHON_BIN="$VENV_DIR/bin/python"
    else
      echo -e "${RED}Critical Error: Python binary not found inside the virtual environment.${NC}"
      exit 1
    fi
  fi
fi

EXPECTED_SHA256="cf02850de3b4b8ae89f8ae5a52c4d682cc6efac4d4e9d61d94681e2710af4ee8"
verify_converter() {
  "$PYTHON_BIN" -c 'import hashlib,sys; sys.exit(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest() != sys.argv[2])' "$LVGL_IMAGE_PY" "$EXPECTED_SHA256"
}
if [ ! -f "$LVGL_IMAGE_PY" ] || ! verify_converter; then
  echo "Downloading LVGLImage.py to $SCRIPT_DIR..."
  curl --fail --location --silent --show-error "$SCRIPT_URL" -o "$LVGL_IMAGE_PY"

  EXPECTED_SHA256="cf02850de3b4b8ae89f8ae5a52c4d682cc6efac4d4e9d61d94681e2710af4ee8"
verify_converter() {
  "$PYTHON_BIN" -c 'import hashlib,sys; sys.exit(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest() != sys.argv[2])' "$LVGL_IMAGE_PY" "$EXPECTED_SHA256"
}
if [ ! -f "$LVGL_IMAGE_PY" ] || ! verify_converter; then
    echo -e "${RED}Error: Failed to download the conversion script.${NC}"
    exit 1
  fi
fi

verify_converter

echo "Checking dependencies (Pillow, PyPNG, LZ4, and NumPy)..."
if ! "$PYTHON_BIN" -c 'import PIL, png, lz4, numpy' 2>/dev/null; then
  "$PYTHON_BIN" -m pip install Pillow pypng lz4 numpy -q
fi

if [ -d "$TEMP_DIR" ]; then
  echo -e "${YELLOW}Removing existing temp directory...${NC}"
  rm -rf "$TEMP_DIR"
fi

echo -e "${YELLOW}Creating temp directory...${NC}"
mkdir -p "$TEMP_DIR"

echo -e "${YELLOW}Copying assets to temp directory...${NC}"
if [ ! -d "$ASSETS_DIR" ]; then
  echo -e "${RED}Error: 'assets' directory not found in $WORKING_DIR!${NC}"
  exit 1
fi
cp -r "$ASSETS_DIR/." "$TEMP_DIR/"

# Generate rotated frames (frame_1, frame_2) from frame_0 sources in temp
ROTATE_SCRIPT="$SCRIPT_DIR/generate_rotated_frames.py"
TEMP_FRAMES_DIR="$TEMP_DIR/frames"
if [ -f "$ROTATE_SCRIPT" ] && [ -d "$TEMP_FRAMES_DIR" ]; then
  echo -e "${YELLOW}Generating rotated frames...${NC}"
  "$PYTHON_BIN" "$ROTATE_SCRIPT" "$TEMP_FRAMES_DIR"
  if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Frame rotation failed.${NC}"
    exit 1
  fi
fi

TARGET_DIRS=("UI" "frames" "icons" "img" "label")

for dir in "${TARGET_DIRS[@]}"; do
  FULL_PATH="$TEMP_DIR/$dir"

  if [ -d "$FULL_PATH" ]; then
    count=$(find "$FULL_PATH" -maxdepth 1 -name "*.png" | wc -l)

    if [ "$count" -eq 0 ]; then
      echo "Directory temp/$dir is empty or contains no .png files. Skipping..."
      continue
    fi

    echo "---"
    echo -e "Processing subdirectory: ${GREEN}$dir${NC}"

    "$PYTHON_BIN" "$LVGL_IMAGE_PY" "$FULL_PATH" --ofmt BIN --cf ARGB8888 --align 1 -o "$FULL_PATH"

    if [ $? -eq 0 ]; then
      echo -e "${GREEN}Success!${NC} Removing original PNG files..."
      find "$FULL_PATH" -name "*.png" -type f -delete
    else
      echo -e "${RED}Conversion error in $dir.${NC}"
      exit 1
    fi
  fi
done

echo -e "\n${GREEN}=======================================${NC}"
echo -e "${GREEN}      Automation Completed Successfully      ${NC}"
echo -e "${GREEN}=======================================${NC}\n"
exit 0
