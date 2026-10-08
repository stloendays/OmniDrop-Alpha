# Stage the official, minimal and isolated Windows CPython distribution.
# This script runs only during packaging: OmniDrop never downloads Python at startup.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$PackageDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Keep the version, URL and integrity pin together. SHA-256 is published by python.org.
$version = "3.13.16"
$expectedSha256 = "97dae5274cc54867065e8d5a3226e48c35017ed332a0fdb0e27d5b5821961297"
$archiveName = "python-$version-embed-amd64.zip"
$url = "https://www.python.org/ftp/python/$version/$archiveName"
$root = (Resolve-Path -LiteralPath $PackageDir).Path
$worker = Join-Path $root "python/worker.py"
$vendor = Join-Path $root "python/vendor"
if (-not (Test-Path -LiteralPath $worker -PathType Leaf)) {
  throw "Install the OmniDrop Python worker before staging the interpreter."
}
if (-not (Test-Path -LiteralPath $vendor -PathType Container)) {
  throw "Install the Python worker dependencies before staging the interpreter."
}

$runtimeDirectory = Join-Path $root "runtime/python"
if (Test-Path -LiteralPath $runtimeDirectory) {
  throw "Refusing to overwrite an existing embedded runtime: $runtimeDirectory"
}
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("omnidrop-embed-" + [guid]::NewGuid().ToString("N"))
New-Item -Path $temporaryDirectory -ItemType Directory -Force | Out-Null
try {
  $archive = Join-Path $temporaryDirectory $archiveName
  Invoke-WebRequest -Uri $url -OutFile $archive
  $digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($digest -ne $expectedSha256) {
    throw "Official CPython archive SHA-256 does not match the pinned release checksum."
  }

  New-Item -Path $runtimeDirectory -ItemType Directory -Force | Out-Null
  Expand-Archive -LiteralPath $archive -DestinationPath $runtimeDirectory

  $python = Join-Path $runtimeDirectory "python.exe"
  $standardLibrary = Join-Path $runtimeDirectory "python313.zip"
  $runtimeDll = Join-Path $runtimeDirectory "python313.dll"
  $isolatedPath = Join-Path $runtimeDirectory "python313._pth"
  foreach ($required in @($python, $standardLibrary, $runtimeDll, $isolatedPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
      throw "Incomplete official CPython embedded archive: $required"
    }
  }

  # The DLL-level ._pth provides full path isolation (no user site packages,
  # registry imports or PYTHONPATH). Explicitly include only our worker and vendor.
  # Do not enable 'import site', which would execute arbitrary .pth entries.
  $isolationPaths = @(
    "python313.zip",
    ".",
    "..\..\python",
    "..\..\python\vendor"
  )
  Set-Content -LiteralPath $isolatedPath -Value $isolationPaths -Encoding ascii

  & $python -B -I -c "import sys, PIL, pypdf; from PIL import Image; from pypdf import PdfReader, PdfWriter; Image.new('RGB', (2, 2)); assert sys.version_info[:3] == (3, 13, 16), sys.version; print('Embedded Python OK:', sys.version.split()[0], PIL.__version__, pypdf.__version__)"
  if ($LASTEXITCODE -ne 0) {
    throw "The embedded Python runtime could not import the staged worker libraries."
  }
  Write-Host "Pinned Python $version embedded and dependencies verified."
}
finally {
  Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
