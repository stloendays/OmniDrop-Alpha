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
$embedded = Join-Path $root "runtime\python\python.exe"

$required = @(
  $gui,
  $cli,
  $worker,
  $embedded,
  (Join-Path $root "runtime\python\python313.dll"),
  (Join-Path $root "runtime\python\python313.zip"),
  (Join-Path $root "runtime\python\python313._pth"),
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

# Check the isolated interpreter and its CPython-3.13 Pillow/pypdf wheels.
$check = (& $embedded -B -I -c "import sys; from PIL import Image; from pypdf import PdfReader, PdfWriter; Image.new('RGB', (2, 2)); assert sys.version_info[:3] == (3, 13, 16); print(sys.executable)" | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $check -ne $embedded) {
  throw "Embedded CPython or vendored dependencies failed: $check"
}

# Remove the CI/setup-python PATH and inherited overrides. Portable releases
# must still run a worker without any Python installed on the user machine.
$oldPath = $env:PATH
$oldPython = [Environment]::GetEnvironmentVariable("OMNIDROP_PYTHON", "Process")
$oldPythonPath = [Environment]::GetEnvironmentVariable("PYTHONPATH", "Process")
try {
  Remove-Item Env:\OMNIDROP_PYTHON -ErrorAction SilentlyContinue
  $env:PYTHONPATH = "C:\omnidrop-nonexistent-global-packages"
  $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"

  $ping = (& $cli worker-ping | Out-String).Trim()
  if ($LASTEXITCODE -ne 0 -or $ping -notmatch '"ok"\s*:\s*true') {
    throw "CLI could not start its packaged worker with system Python unavailable: $ping"
  }
  $capabilities = (& $cli capabilities | Out-String).Trim()
  if ($LASTEXITCODE -ne 0 -or $capabilities -notmatch "image.compress" -or
      $capabilities -notmatch "pdf.extract_text") {
    throw "Packaged Pillow/pypdf capabilities unavailable: $capabilities"
  }
}
finally {
  $env:PATH = $oldPath
  if ($null -eq $oldPython) {
    Remove-Item Env:\OMNIDROP_PYTHON -ErrorAction SilentlyContinue
  } else {
    $env:OMNIDROP_PYTHON = $oldPython
  }
  if ($null -eq $oldPythonPath) {
    Remove-Item Env:\PYTHONPATH -ErrorAction SilentlyContinue
  } else {
    $env:PYTHONPATH = $oldPythonPath
  }
}

# Verify a complete installed-package local workflow, not just worker ping.
$workflowFile = Join-Path $root "workflows\examples\text-clean.omniworkflow.json"
if (-not (Test-Path $workflowFile)) {
  throw "Packaged workflow examples are missing."
}
$smokeDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("OmniDropAlphaSmoke-" + [guid]::NewGuid().ToString("N"))
New-Item -Path $smokeDirectory -ItemType Directory -Force | Out-Null
try {
  $source = Join-Path $smokeDirectory "sample.txt"
  [System.IO.File]::WriteAllText($source, "alpha  `nalpha  `n", [System.Text.UTF8Encoding]::new($false))

  $validated = (& $cli workflow validate $workflowFile | Out-String).Trim()
  if ($LASTEXITCODE -ne 0 -or $validated -notmatch '"node_count"') {
    throw "Packaged Workflow validation failed: $validated"
  }

  $resultText = (& $cli workflow run $workflowFile $source | Out-String).Trim()
  if ($LASTEXITCODE -ne 0) {
    throw "Packaged Workflow run failed: $resultText"
  }
  $result = $resultText | ConvertFrom-Json
  if (-not $result.ok -or $result.output_paths.Count -ne 1) {
    throw "Packaged Workflow result is invalid: $resultText"
  }
  if (-not (Test-Path $result.output_paths[0])) {
    throw "Packaged Workflow did not generate its reported output."
  }
  if ([System.IO.File]::ReadAllText($source) -ne "alpha  `nalpha  `n") {
    throw "Packaged Workflow unexpectedly modified the original file."
  }

  # The installed CLI must perform a real, reversible, no-overwrite rename
  # without depending on anything in the developer source/build directory.
  $renameInput = Join-Path $smokeDirectory "draft-package.txt"
  $renameOutput = Join-Path $smokeDirectory "final-package.txt"
  [System.IO.File]::WriteAllText($renameInput, "local rename test")
  $oldRenameJournal = [Environment]::GetEnvironmentVariable("OMNIDROP_RENAME_JOURNAL_DIR", "Process")
  try {
    $env:OMNIDROP_RENAME_JOURNAL_DIR = Join-Path $smokeDirectory "rename-journals"
    $planText = (& $cli rename-prepare --find "draft-" --replace "final-" $renameInput | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Packaged rename preparation failed: $planText" }
    $plan = $planText | ConvertFrom-Json
    if (-not $plan.ok -or $plan.state -ne "prepared" -or
        -not (Test-Path $renameInput) -or (Test-Path $renameOutput)) {
      throw "Packaged rename prepare unexpectedly changed the filesystem."
    }
    $id = $plan.transaction_id
    $commitText = (& $cli rename-apply $id --confirm | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Packaged rename apply failed: $commitText" }
    $commit = $commitText | ConvertFrom-Json
    if (-not $commit.ok -or $commit.state -ne "committed" -or
        (Test-Path $renameInput) -or -not (Test-Path $renameOutput)) {
      throw "Packaged rename apply did not perform the expected safe move."
    }
    $undoText = (& $cli rename-undo $id --confirm | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Packaged rename undo failed: $undoText" }
    $undo = $undoText | ConvertFrom-Json
    if (-not $undo.ok -or $undo.state -ne "undone" -or
        -not (Test-Path $renameInput) -or (Test-Path $renameOutput)) {
      throw "Packaged rename Undo did not restore the original path."
    }
    if ([System.IO.File]::ReadAllText($renameInput) -ne "local rename test") {
      throw "Packaged rename transaction changed file contents."
    }
  }
  finally {
    if ($null -eq $oldRenameJournal) {
      Remove-Item Env:\OMNIDROP_RENAME_JOURNAL_DIR -ErrorAction SilentlyContinue
    } else {
      $env:OMNIDROP_RENAME_JOURNAL_DIR = $oldRenameJournal
    }
  }
}
finally {
  Remove-Item -Path $smokeDirectory -Recurse -Force -ErrorAction SilentlyContinue
}

$first = $null
$second = $null

try {
  $first = Start-Process -FilePath $gui -WorkingDirectory $root -PassThru

  # A fixed 1.8-second sleep raced with overloaded Windows CI runners.
  # Wait for the GUI message loop to become responsive before probing the
  # single-instance IPC contract. A timeout is still a real test failure.
  $ready = $false
  $readyDeadline = (Get-Date).AddSeconds(15)
  do {
    $first.Refresh()
    if ($first.HasExited) {
      throw "Packaged OmniDrop GUI exited during startup with code $($first.ExitCode)."
    }
    try {
      if ($first.WaitForInputIdle(750)) {
        $ready = $true
        break
      }
    }
    catch [System.InvalidOperationException] {
      # The GUI process can briefly have no message loop during Qt startup.
    }
    Start-Sleep -Milliseconds 150
  } while ((Get-Date) -lt $readyDeadline)

  if (-not $ready) {
    throw "Packaged OmniDrop GUI did not become responsive within 15 seconds (pid $($first.Id))."
  }

  $second = Start-Process -FilePath $gui -WorkingDirectory $root -PassThru
  if (-not $second.WaitForExit(12000)) {
    $first.Refresh()
    Stop-Process -Id $second.Id -Force -ErrorAction SilentlyContinue
    throw "Secondary OmniDrop did not forward in 12 seconds. Primary pid=$($first.Id), primaryExited=$($first.HasExited), secondaryPid=$($second.Id)."
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
