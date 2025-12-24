# ==============================================================================
# LVGL Image Conversion Automation Script (Windows Version)
# ==============================================================================

# Terminal Color Configuration
$Yellow = "$([char]27)[1;33m"
$Green  = "$([char]27)[0;32m"
$Red    = "$([char]27)[0;31m"
$NC     = "$([char]27)[0m"
$ROOT_DIR = Get-Location
$VENV_DIR = Join-Path $ROOT_DIR "lvgl-env"
$PYTHON_EXE = Join-Path $VENV_DIR "Scripts\python.exe"
$PYTHON_SCRIPT = Join-Path $ROOT_DIR "LVGLImage.py"
$ASSETS_DIR = Join-Path $ROOT_DIR "assets"
$TEMP_DIR = Join-Path $ROOT_DIR "temp"

Write-Host "`n${Yellow}>>> Starting LVGL Assets Automation${NC}"

if (-not (Test-Path $PYTHON_EXE)) {
    Write-Host "${Yellow}Virtual environment 'lvgl-env' not found. Creating...${NC}"
    python -m venv lvgl-env
    if ($LASTEXITCODE -ne 0) {
        Write-Host "${Red}Critical Error: Failed to create Python virtual environment.${NC}"
        exit 1
    }
}

if (-not (Test-Path $PYTHON_SCRIPT)) {
    Write-Host "Downloading LVGLImage.py from official repository..."
    $URL = "https://raw.githubusercontent.com/lvgl/lvgl/master/scripts/LVGLImage.py"
    try {
        Invoke-WebRequest -Uri $URL -OutFile $PYTHON_SCRIPT -ErrorAction Stop
    } catch {
        Write-Host "${Red}Error: Failed to download the conversion script.${NC}"
        exit 1
    }
}

Write-Host "Checking dependencies (Pillow, PyPNG, and LZ4)..."
& $PYTHON_EXE -m pip install --upgrade pip -q
& $PYTHON_EXE -m pip install Pillow pypng lz4 -q

if (Test-Path $TEMP_DIR) {
    Write-Host "${Yellow}Removing existing temp directory...${NC}"
    Remove-Item -Path $TEMP_DIR -Recurse -Force
}

Write-Host "${Yellow}Copying assets to temp directory...${NC}"
Copy-Item -Path $ASSETS_DIR -Destination $TEMP_DIR -Recurse

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
        
        $errorOut = & $PYTHON_EXE "$PYTHON_SCRIPT" "$FULL_PATH" --ofmt BIN --cf ARGB8888 --align 1 -o "$FULL_PATH" 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "${Green}Success!${NC} Removing original PNG files..."
            Get-ChildItem -Path $FULL_PATH -Filter "*.png" | Remove-Item -Force
        } else {
            Write-Host "${Red}Conversion error in $dir :${NC}"
            Write-Host $errorOut
        }
    }
}

Write-Host "`n${Green}=======================================${NC}"
Write-Host "${Green}      Automation Completed Successfully ${NC}"
Write-Host "${Green}=======================================${NC}`n"
exit 0