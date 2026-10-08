# Authenticode-sign OmniDrop-owned executables only. Requires an actual trusted
# code-signing certificate supplied as protected GitHub Actions secrets.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$PackageDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$encodedPfx = $env:OMNIDROP_CODESIGN_PFX_BASE64
$passphrase = $env:OMNIDROP_CODESIGN_PASSWORD
if ([string]::IsNullOrWhiteSpace($encodedPfx) -and
    [string]::IsNullOrWhiteSpace($passphrase)) {
  Write-Warning "No Authenticode certificate configured. Binaries remain unsigned; the ZIP will still receive GitHub Sigstore provenance."
  return
}
if ([string]::IsNullOrWhiteSpace($encodedPfx) -or
    [string]::IsNullOrWhiteSpace($passphrase)) {
  throw "Both OMNIDROP_CODESIGN_PFX_BASE64 and OMNIDROP_CODESIGN_PASSWORD are required."
}

$root = (Resolve-Path -LiteralPath $PackageDir).Path
$executables = @(
  (Join-Path $root "OmniDrop.exe"),
  (Join-Path $root "omnidrop-cli.exe")
)
foreach ($file in $executables) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
    throw "Cannot sign a missing OmniDrop executable: $file"
  }
}

$signTool = Get-Command "signtool.exe" -ErrorAction SilentlyContinue
if ($signTool) {
  $signToolPath = $signTool.Source
}
else {
  $sdkRoot = Join-Path "${env:ProgramFiles(x86)}" "Windows Kits/10/bin"
  $sdkTools = @()
  if (Test-Path -LiteralPath $sdkRoot) {
    $sdkTools = @(Get-ChildItem -LiteralPath $sdkRoot -Filter signtool.exe -File -Recurse |
      Where-Object { $_.FullName -match '[\\/]x64[\\/]signtool\.exe$' } |
      Sort-Object FullName -Descending)
  }
  if ($sdkTools.Count -eq 0) {
    throw "Windows SDK signtool.exe is required for Authenticode signing."
  }
  $signToolPath = $sdkTools[0].FullName
}

$temporaryPfx = Join-Path ([System.IO.Path]::GetTempPath()) ("omnidrop-sign-" + [guid]::NewGuid().ToString("N") + ".pfx")
$importedCertificate = $null
try {
  [System.IO.File]::WriteAllBytes($temporaryPfx, [Convert]::FromBase64String($encodedPfx))
  $securePassword = ConvertTo-SecureString $passphrase -AsPlainText -Force
  $importedCertificate = Import-PfxCertificate -FilePath $temporaryPfx -Password $securePassword -CertStoreLocation "Cert:\CurrentUser\My" -Exportable:$false
  if (-not $importedCertificate -or -not $importedCertificate.HasPrivateKey) {
    throw "The imported code-signing certificate has no usable private key."
  }
  $now = Get-Date
  if ($now -lt $importedCertificate.NotBefore -or $now -ge $importedCertificate.NotAfter) {
    throw "The code-signing certificate is not currently valid."
  }

  $allowsCodeSigning = $false
  foreach ($extension in $importedCertificate.Extensions) {
    if ($extension -is [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]) {
      foreach ($usage in $extension.EnhancedKeyUsages) {
        if ($usage.Value -eq "1.3.6.1.5.5.7.3.3") {
          $allowsCodeSigning = $true
        }
      }
    }
  }
  if (-not $allowsCodeSigning) {
    throw "Certificate is not authorized for code-signing EKU."
  }

  foreach ($file in $executables) {
    & $signToolPath sign /fd SHA256 /td SHA256 /tr "http://timestamp.digicert.com" /sha1 $importedCertificate.Thumbprint /s My $file
    if ($LASTEXITCODE -ne 0) {
      throw "Authenticode signing failed: $file"
    }
    & $signToolPath verify /pa /v $file
    if ($LASTEXITCODE -ne 0) {
      throw "Authenticode verification failed: $file"
    }
  }
  Write-Host "OmniDrop Authenticode signing and verification passed for both owned executables."
}
finally {
  if ($importedCertificate) {
    Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $importedCertificate.Thumbprint) -Force -ErrorAction SilentlyContinue
  }
  Remove-Item -LiteralPath $temporaryPfx -Force -ErrorAction SilentlyContinue
}
