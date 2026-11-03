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
# LVGL Image Conversion Automation Script (Windows Version)
# ==============================================================================

$ErrorActionPreference = "Stop"

# Terminal Color Configuration
$Yellow = "$([char]27)[1;33m"
$Green  = "$([char]27)[0;32m"
$Red    = "$([char]27)[0;31m"
$NC     = "$([char]27)[0m"

$SCRIPT_DIR = Split-Path -Parent $MyInvocation.MyCommand.Definition
$VENV_DIR = Join-Path $SCRIPT_DIR "lvgl-env"
$PYTHON_EXE = Join-Path $VENV_DIR "Scripts\python.exe"
$PYTHON_SCRIPT = Join-Path $SCRIPT_DIR "LVGLImage.py"

$WORKING_DIR = Get-Location
$ASSETS_DIR = Join-Path $WORKING_DIR "assets"
$TEMP_DIR = if ($env:HIGHBOY_ASSETS_OUTPUT) { $env:HIGHBOY_ASSETS_OUTPUT } else { Join-Path $WORKING_DIR "temp" }

Write-Host "`n${Yellow}>>> Starting LVGL Assets Automation${NC}"

if (-not (Test-Path $PYTHON_EXE)) {
    Write-Host "${Yellow}Virtual environment 'lvgl-env' not found in $SCRIPT_DIR. Creating...${NC}"
    $HOST_PYTHON = if ($env:HIGHBOY_ASSETS_PYTHON) { $env:HIGHBOY_ASSETS_PYTHON } else { "python" }
    & $HOST_PYTHON -m venv "$VENV_DIR"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "${Red}Critical Error: Failed to create Python virtual environment.${NC}"
        exit 1
    }
}

$EXPECTED_SHA256 = "cf02850de3b4b8ae89f8ae5a52c4d682cc6efac4d4e9d61d94681e2710af4ee8"
if ((-not (Test-Path $PYTHON_SCRIPT)) -or
    ((Get-FileHash $PYTHON_SCRIPT -Algorithm SHA256).Hash -ne $EXPECTED_SHA256)) {
    Write-Host "Downloading LVGLImage.py to $SCRIPT_DIR..."
    $URL = "https://raw.githubusercontent.com/lvgl/lvgl/v9.4.0/scripts/LVGLImage.py"
    try {
        Invoke-WebRequest -Uri $URL -OutFile $PYTHON_SCRIPT -ErrorAction Stop
    } catch {
        Write-Host "${Red}Error: Failed to download the conversion script.${NC}"
        exit 1
    }
}

if ((Get-FileHash $PYTHON_SCRIPT -Algorithm SHA256).Hash -ne $EXPECTED_SHA256) {
    throw "LVGL v9.4.0 converter checksum mismatch"
}
Write-Host "Checking dependencies (Pillow, PyPNG, LZ4, and NumPy)..."
& "$PYTHON_EXE" -c 'import PIL, png, lz4, numpy'
if ($LASTEXITCODE -ne 0) {
    & "$PYTHON_EXE" -m pip install Pillow pypng lz4 numpy -q
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

if (Test-Path $TEMP_DIR) {
    Write-Host "${Yellow}Removing existing temp directory...${NC}"
    Remove-Item -Path $TEMP_DIR -Recurse -Force
}

Write-Host "${Yellow}Copying assets to temp directory...${NC}"
if (-not (Test-Path $ASSETS_DIR)) {
    Write-Host "${Red}Error: 'assets' directory not found in $WORKING_DIR!${NC}"
    exit 1
}
Copy-Item -Path $ASSETS_DIR -Destination $TEMP_DIR -Recurse

# Generate rotated frames (frame_1, frame_2) from frame_0 sources in temp
$ROTATE_SCRIPT = Join-Path $SCRIPT_DIR "generate_rotated_frames.py"
$TEMP_FRAMES_DIR = Join-Path $TEMP_DIR "frames"
if ((Test-Path $ROTATE_SCRIPT) -and (Test-Path $TEMP_FRAMES_DIR)) {
    Write-Host "${Yellow}Generating rotated frames...${NC}"
    & "$PYTHON_EXE" "$ROTATE_SCRIPT" "$TEMP_FRAMES_DIR"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "${Red}Error: Frame rotation failed.${NC}"
        exit 1
    }
}

$TARGET_DIRS = @("UI", "frames", "icons", "img", "label")
foreach ($dir in $TARGET_DIRS) {
    $FULL_PATH = Join-Path $TEMP_DIR $dir
    if (Test-Path $FULL_PATH) {
        $pngFiles = Get-ChildItem -Path $FULL_PATH -Filter "*.png"
        
        if ($null -eq $pngFiles) {
            Write-Host "Directory temp/$dir is empty or contains no .png files. Skipping..."
            continue
        }
        Write-Host "---"
        Write-Host "Processing subdirectory: ${Green}$dir${NC}"
        
        $errorOut = & "$PYTHON_EXE" "$PYTHON_SCRIPT" "$FULL_PATH" --ofmt BIN --cf ARGB8888 --align 1 -o "$FULL_PATH" 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "${Green}Success!${NC} Removing original PNG files..."
            Get-ChildItem -Path $FULL_PATH -Filter "*.png" | Remove-Item -Force
        } else {
            Write-Host "${Red}Conversion error in $dir :${NC}"
            Write-Host $errorOut
            exit 1
        }
    }
}

Write-Host "`n${Green}=======================================${NC}"
Write-Host "${Green}      Automation Completed Successfully ${NC}"
Write-Host "${Green}=======================================${NC}`n"
exit 0