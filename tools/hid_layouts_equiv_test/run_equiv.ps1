# Build and run the HID layouts equivalence test (Windows).
# Usage: powershell -ExecutionPolicy Bypass -File .\run_equiv.ps1
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$cc = $null
foreach ($cand in @('gcc', 'clang', 'cc')) {
  if (Get-Command $cand -ErrorAction SilentlyContinue) { $cc = $cand; break }
}
if (-not $cc) {
  Write-Error "No C compiler found on PATH (looked for gcc, clang, cc)."
  exit 2
}

$newSrc = '..\..\firmware_p4\components\Applications\bad_usb\hid_layouts.c'

Write-Host "Compiling with: $cc"
& $cc -std=c11 -Wall -Wno-unused-variable -I shims `
  equiv_test.c hid_layouts_reference_old.c $newSrc `
  -o equiv_test.exe
if ($LASTEXITCODE -ne 0) { Write-Error "Compilation failed."; exit 1 }

Write-Host "Running:"
& .\equiv_test.exe
exit $LASTEXITCODE
