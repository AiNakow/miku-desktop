param(
  [Parameter(Mandatory = $true)]
  [string]$AppDir,

  [string]$ExeName = "MikuPet.exe"
)

$ErrorActionPreference = "Stop"
$hasNonArm64 = $false

function Get-MachineType {
  param([string]$FilePath)

  $bytes = [System.IO.File]::ReadAllBytes($FilePath)
  if ($bytes.Length -lt 0x100) { return "Unknown" }

  # DOS header -> e_lfanew at 0x3C
  $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
  if ($peOffset + 6 -gt $bytes.Length) { return "Unknown" }

  # PE signature "PE\0\0"
  if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45) { return "Unknown" }

  # IMAGE_FILE_HEADER.Machine at PE+4
  $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)

  switch ($machine) {
    0x8664 { "x64" }
    0xAA64 { "ARM64" }
    0x014C { "x86" }
    default { ("0x{0:X4}" -f $machine) }
  }
}

if (-not (Test-Path $AppDir)) {
  throw "App directory not found: $AppDir"
}

$exePath = Join-Path $AppDir $ExeName
if (-not (Test-Path $exePath)) {
  throw "Executable not found: $exePath"
}

Write-Host "=== Host Architecture ==="
Write-Host "PROCESSOR_ARCHITECTURE=$env:PROCESSOR_ARCHITECTURE"
Write-Host ""

Write-Host "=== Target Executable Architecture ==="
$exeArch = Get-MachineType -FilePath $exePath
Write-Host "$ExeName -> $exeArch"
Write-Host ""

Write-Host "=== Scan EXE/DLL Architectures In Directory ==="
$files = Get-ChildItem -Path "$AppDir\*" -File -Include *.exe,*.dll
$report = foreach ($f in $files) {
  [PSCustomObject]@{
    File = $f.Name
    Arch = Get-MachineType -FilePath $f.FullName
  }
}
$report | Sort-Object Arch, File | Format-Table -AutoSize

$bad = $report | Where-Object { $_.Arch -eq "x64" -or $_.Arch -eq "x86" }
if ($bad) {
  $hasNonArm64 = $true
  Write-Host ""
  Write-Warning "Found non-ARM64 binaries. This can cause 0xc000007b:"
  $bad | Format-Table -AutoSize
} else {
  Write-Host ""
  Write-Host "No x64/x86 binaries found (static PE header check)."
}

if ($hasNonArm64) {
  exit 1
}

exit 0