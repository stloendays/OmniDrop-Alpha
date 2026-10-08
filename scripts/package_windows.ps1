# Produce a complete Windows portable ZIP from a Release build.
# Use -StageOnly then sign_windows.ps1 then -FinalizeOnly for signed releases.
[CmdletBinding()]
param(
  [string]$BuildDir = "$PSScriptRoot\..\build",
  [string]$PackageDir = "$PSScriptRoot\..\package",
  [string]$OutputZip = "",
  [switch]$StageOnly,
  [switch]$FinalizeOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
  throw "OmniDrop Windows packaging requires Windows."
}
if ($StageOnly -and $FinalizeOnly) {
  throw "-StageOnly and -FinalizeOnly cannot be used together."
}

if (-not $FinalizeOnly) {
  if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
    throw "Build directory not found: $BuildDir"
  }
  # Native Pillow wheels must be built for our embedded CPython 3.13 ABI.
  & python -c "import sys; assert sys.version_info[:2] == (3,13), 'Packaging requires Python 3.13'"
  if ($LASTEXITCODE -ne 0) {
    throw "Use Python 3.13 to stage Windows native wheels."
  }

  if (Test-Path -LiteralPath $PackageDir) {
    Remove-Item -LiteralPath $PackageDir -Recurse -Force
  }
  & cmake --install $BuildDir --config Release --prefix $PackageDir
  if ($LASTEXITCODE -ne 0) { throw "CMake install failed." }

  $root = (Resolve-Path -LiteralPath $PackageDir).Path
  $gui = Join-Path $root "OmniDrop.exe"
  $windeployqt = (Get-Command windeployqt.exe -ErrorAction Stop).Source
  & $windeployqt --release --no-translations $gui
  if ($LASTEXITCODE -ne 0) { throw "windeployqt failed." }

  & python -m pip install --disable-pip-version-check --no-input --target (Join-Path $root "python/vendor") -r "$PSScriptRoot\..\python\requirements.txt"
  if ($LASTEXITCODE -ne 0) { throw "Vendoring worker dependencies failed." }

  & "$PSScriptRoot/stage_embedded_python.ps1" -PackageDir $root
}
if ($StageOnly) {
  Write-Host "Windows runtime staged. Sign OmniDrop-owned executables before finalizing."
  return
}

if (-not (Test-Path -LiteralPath $PackageDir -PathType Container)) {
  throw "Package directory not found: $PackageDir"
}
$root = (Resolve-Path -LiteralPath $PackageDir).Path
$cli = Join-Path $root "omnidrop-cli.exe"
$version = (& $cli --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($version)) {
  throw "Cannot determine packaged OmniDrop version."
}

# Smoke and archiving receive NO code-signing credentials from CI.
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
