param(
  [Parameter(Mandatory = $true)]
  [string]$PackageDir,

  [Parameter(Mandatory = $true)]
  [string]$ExpectedVersion
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path $PackageDir).Path
$gui = Join-Path $root "OmniDrop.exe"
$cli = Join-Path $root "omnidrop-cli.exe"
$worker = Join-Path $root "python\worker.py"

$required = @(
  $gui,
  $cli,
  $worker,
  (Join-Path $root "Qt6Core.dll"),
  (Join-Path $root "Qt6Gui.dll"),
  (Join-Path $root "Qt6Widgets.dll"),
  (Join-Path $root "Qt6Network.dll"),
  (Join-Path $root "platforms\qwindows.dll")
)

foreach ($path in $required) {
  if (-not (Test-Path $path)) {
    throw "Packaged runtime is missing required file: $path"
  }
}

$cliVersion = (& $cli --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
  throw "Packaged CLI failed to start."
}
if ($cliVersion -ne $ExpectedVersion) {
  throw "Packaged CLI version mismatch. Expected $ExpectedVersion, got $cliVersion"
}

$ping = (& $cli worker-ping | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
  throw "Packaged CLI could not start the local Python worker."
}
if ($ping -notmatch '"ok"\s*:\s*true') {
  throw "Packaged worker ping did not return a successful JSON response: $ping"
}

$first = $null
$second = $null

try {
  $first = Start-Process -FilePath $gui -WorkingDirectory $root -PassThru
  Start-Sleep -Milliseconds 1800

  if ($first.HasExited) {
    throw "Packaged OmniDrop GUI exited during startup with code $($first.ExitCode)."
  }

  $second = Start-Process -FilePath $gui -WorkingDirectory $root -PassThru
  if (-not $second.WaitForExit(5000)) {
    Stop-Process -Id $second.Id -Force -ErrorAction SilentlyContinue
    throw "A second packaged OmniDrop launch did not forward to the primary instance and exit."
  }
  if ($second.ExitCode -ne 0) {
    throw "Second packaged OmniDrop launch exited with code $($second.ExitCode)."
  }

  if ($first.HasExited) {
    throw "Primary packaged OmniDrop instance exited after second-launch activation."
  }
}
finally {
  if ($second -and -not $second.HasExited) {
    Stop-Process -Id $second.Id -Force -ErrorAction SilentlyContinue
  }

  if ($first -and -not $first.HasExited) {
    [void]$first.CloseMainWindow()
    if (-not $first.WaitForExit(3000)) {
      Stop-Process -Id $first.Id -Force -ErrorAction SilentlyContinue
    }
  }
}

Write-Host "Windows package smoke test passed for OmniDrop $ExpectedVersion."
