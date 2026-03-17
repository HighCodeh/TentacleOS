#!/bin/bash

# ==============================================================================
# LVGL Image Conversion Automation Script (Linux/macOS Version)
# ==============================================================================

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
SCRIPT_URL="https://raw.githubusercontent.com/lvgl/lvgl/master/scripts/LVGLImage.py"

# Caminhos de trabalho (baseados no diretório de execução do CMake)
WORKING_DIR=$(pwd)
ASSETS_DIR="$WORKING_DIR/assets"
TEMP_DIR="$WORKING_DIR/temp"

echo -e "\n${YELLOW}>>> Starting LVGL Assets Automation (Linux/Mac)${NC}"

if [ ! -f "$PYTHON_BIN" ]; then
  echo -e "${YELLOW}Virtual environment 'lvgl-env' not found in $SCRIPT_DIR. Creating...${NC}"
  rm -rf "$VENV_DIR"
  python3 -m venv "$VENV_DIR"

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

if [ ! -f "$LVGL_IMAGE_PY" ]; then
  echo "Downloading LVGLImage.py to $SCRIPT_DIR..."
  wget -q "$SCRIPT_URL" -O "$LVGL_IMAGE_PY" || curl -s "$SCRIPT_URL" -o "$LVGL_IMAGE_PY"

  if [ ! -f "$LVGL_IMAGE_PY" ]; then
    echo -e "${RED}Error: Failed to download the conversion script.${NC}"
    exit 1
  fi
fi

echo "Checking dependencies (Pillow, PyPNG, and LZ4)..."
"$PYTHON_BIN" -m pip install --upgrade pip -q
"$PYTHON_BIN" -m pip install Pillow pypng lz4 -q

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
    fi
  fi
done

echo -e "\n${GREEN}=======================================${NC}"
echo -e "${GREEN}      Automation Completed Successfully      ${NC}"
echo -e "${GREEN}=======================================${NC}\n"
exit 0
