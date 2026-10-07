param(
  [string]$BuildDir = "$PSScriptRoot\..\build",
  [string]$PackageDir = "$PSScriptRoot\..\package",
  [string]$OutputZip = "$PSScriptRoot\..\OmniDrop-windows-dev.zip"
)

$ErrorActionPreference = "Stop"

if (Test-Path $PackageDir) {
  Remove-Item -Recurse -Force $PackageDir
}

cmake --install $BuildDir --config Release --prefix $PackageDir
python -m pip install --target "$PackageDir\python\vendor" -r "$PSScriptRoot\..\python\requirements.txt"

if (Test-Path $OutputZip) {
  Remove-Item -Force $OutputZip
}
Compress-Archive -Path "$PackageDir\*" -DestinationPath $OutputZip
Write-Host "Created $OutputZip"
