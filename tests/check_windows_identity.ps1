param(
  [Parameter(Mandatory = $true)]
  [string]$Executable,

  [Parameter(Mandatory = $true)]
  [string]$ExpectedVersion
)

$info = (Get-Item -LiteralPath $Executable).VersionInfo

if ($info.ProductName -ne "OmniDrop") {
  throw "Expected ProductName=OmniDrop, got '$($info.ProductName)'."
}

if (-not $info.FileVersion.StartsWith($ExpectedVersion)) {
  throw "Expected FileVersion to start with $ExpectedVersion, got '$($info.FileVersion)'."
}

if ($info.FileDescription -notlike "OmniDrop*") {
  throw "Expected an OmniDrop FileDescription, got '$($info.FileDescription)'."
}

Add-Type -AssemblyName System.Drawing
$icon = [System.Drawing.Icon]::ExtractAssociatedIcon($Executable)
if ($null -eq $icon -or $icon.Width -le 0 -or $icon.Height -le 0) {
  throw "No executable icon could be extracted."
}
$icon.Dispose()

Write-Host "Windows identity OK: $($info.ProductName) $($info.FileVersion)"
