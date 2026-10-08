# Produce a complete Windows portable ZIP, including verified Python and Qt.
# Run from a configured/built Release tree on Windows with Python 3.13.
[CmdletBinding()]
param(
  [string]$BuildDir = "$PSScriptRoot\..\build",
  [string]$PackageDir = "$PSScriptRoot\..\package",
  [string]$OutputZip = ""
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
  throw "OmniDrop Windows packaging requires Windows."
}
if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
  throw "Build directory not found: $BuildDir"
}

# The vendored Pillow wheel must match the embedded cp313 interpreter ABI.
& python -c "import sys; assert sys.version_info[:2] == (3,13), 'Packaging requires Python 3.13'"
if ($LASTEXITCODE -ne 0) {
  throw "Use a Python 3.13 build environment to install compatible wheels."
}

if (Test-Path -LiteralPath $PackageDir) {
  Remove-Item -LiteralPath $PackageDir -Recurse -Force
}
& cmake --install $BuildDir --config Release --prefix $PackageDir
if ($LASTEXITCODE -ne 0) { throw "CMake install failed." }

$root = (Resolve-Path -LiteralPath $PackageDir).Path
$gui = Join-Path $root "OmniDrop.exe"
$cli = Join-Path $root "omnidrop-cli.exe"
$windeployqt = (Get-Command windeployqt.exe -ErrorAction Stop).Source
& $windeployqt --release --no-translations $gui
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }

& python -m pip install --disable-pip-version-check --no-input --target (Join-Path $root "python/vendor") -r "$PSScriptRoot\..\python\requirements.txt"
if ($LASTEXITCODE -ne 0) { throw "Vendoring worker dependencies failed." }

& "$PSScriptRoot/stage_embedded_python.ps1" -PackageDir $root
& "$PSScriptRoot/sign_windows.ps1" -PackageDir $root

$version = (& $cli --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($version)) {
  throw "Cannot determine the installed OmniDrop version."
}
& "$PSScriptRoot/../tests/smoke_windows_package.ps1" -PackageDir $root -ExpectedVersion $version

if ([string]::IsNullOrWhiteSpace($OutputZip)) {
  $OutputZip = Join-Path "$PSScriptRoot\.." "OmniDrop-$version-windows-dev.zip"
}
$OutputZip = [IO.Path]::GetFullPath($OutputZip)
if (Test-Path -LiteralPath $OutputZip) {
  Remove-Item -LiteralPath $OutputZip -Force
}
Compress-Archive -Path (Join-Path $root "*") -DestinationPath $OutputZip -Force
$hash = (Get-FileHash -LiteralPath $OutputZip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($OutputZip))" |
  Set-Content -LiteralPath "$OutputZip.sha256" -Encoding ascii
Write-Host "Packaged: $OutputZip"
Write-Host "Archive SHA-256: $hash"
