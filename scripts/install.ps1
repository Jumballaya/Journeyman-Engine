# Installs the Journeyman CLI (jm and the engine) on Windows from a GitHub release.
#
#   irm https://github.com/Jumballaya/Journeyman-Engine/releases/latest/download/install.ps1 | iex
#   & ([scriptblock]::Create((irm <same url>))) -Editor     the editor too, in <dir>\editor
#
# JM_VERSION=v0.0.1   a release (default: the one this script came with)
# JM_INSTALL_DIR=dir  where it goes (default: ~\.jm); jm lands in <dir>\bin
# JM_FROM=dir         install from release files already in dir (CI, offline)
#
# Everything it does is logged to <dir>\install.log, printed when it fails.
# Never prompts. Adds <dir>\bin to the user's PATH (new terminals see it).
param([switch]$Editor)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue' # Invoke-WebRequest's progress bar is slow

$repo = 'https://github.com/Jumballaya/Journeyman-Engine/releases'
$version = if ($env:JM_VERSION) { $env:JM_VERSION } else { 'latest' }
$dir = if ($env:JM_INSTALL_DIR) { $env:JM_INSTALL_DIR } else { Join-Path $HOME '.jm' }
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$dir = (Resolve-Path $dir).Path
$log = Join-Path $dir 'install.log'
Set-Content -Path $log -Value "install.ps1 $((Get-Date).ToUniversalTime().ToString('s'))Z on Windows, into $dir"

function Say($text) { Write-Host $text; Add-Content -Path $log -Value $text }
# Fail throws, never exits: run as `irm | iex`, exit would close the user's window.
function Fail($text) { throw $text }

$platform = 'windows-amd64'
$base = if ($version -eq 'latest') { "$repo/latest/download" } else { "$repo/download/$version" }
# Beside the install, not in %TEMP%: Move-Item can't move a folder to another drive.
$tmp = Join-Path $dir (".install-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null

# Fetch puts one of the release's files in $tmp, checked against SHA256SUMS.
function Fetch($file) {
  try {
    if ($env:JM_FROM) {
      Say "Copying $file from $env:JM_FROM"
      Copy-Item (Join-Path $env:JM_FROM $file), (Join-Path $env:JM_FROM 'SHA256SUMS') $tmp
    } else {
      Say "Downloading $file ($version)"
      Invoke-WebRequest "$base/$file" -OutFile (Join-Path $tmp $file) -UseBasicParsing
      Invoke-WebRequest "$base/SHA256SUMS" -OutFile (Join-Path $tmp 'SHA256SUMS') -UseBasicParsing
    }
  } catch { Fail "couldn't get ${file}: $_" }
  $line = Get-Content (Join-Path $tmp 'SHA256SUMS') | Where-Object { $_ -match " \*?$([regex]::Escape($file))$" } | Select-Object -First 1
  if (-not $line) { Fail "SHA256SUMS has no entry for $file" }
  $expected = ($line -split '\s+')[0]
  $actual = (Get-FileHash (Join-Path $tmp $file) -Algorithm SHA256).Hash
  if ($actual -ne $expected) { Fail "checksum mismatch for ${file}: the download is damaged or not the release's" } # -ne ignores case
}

# Swap puts folder $from at $to. The old $to is moved aside (a running jm or
# editor makes that fail, leaving it untouched) and kept until the install checks
# out: a failure anywhere puts every one back.
$backups = [System.Collections.Generic.List[object]]::new() # one list in every scope
function Swap($from, $to) {
  if (Test-Path $to) {
    $old = "$to.old-" + [Guid]::NewGuid()
    try { Move-Item $to $old } catch { Fail "couldn't replace $to (is jm or the editor running?): $_" }
    $backups.Add(@($to, $old))
  }
  try { Move-Item $from $to } catch { Fail "couldn't write ${to}: $_" }
}

try {
  if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { Fail "no build for a $env:PROCESSOR_ARCHITECTURE CPU (there's windows-amd64)" }
  $file = "journeyman-cli-$platform.zip"
  Fetch $file
  Expand-Archive (Join-Path $tmp $file) -DestinationPath $tmp
  Swap (Join-Path $tmp "journeyman-cli-$platform") (Join-Path $dir 'bin')

  if ($Editor) {
    $file = "journeyman-editor-$platform.zip"
    Fetch $file
    Expand-Archive (Join-Path $tmp $file) -DestinationPath $tmp
    Swap (Join-Path $tmp "journeyman-editor-$platform") (Join-Path $dir 'editor')
    Say "Installed the editor in $dir\editor (run it with: jm editor)"
  }

  $jm = Join-Path $dir 'bin\jm.exe'
  $installed = & $jm --version 2>>$log
  if ($LASTEXITCODE -ne 0) { Fail "the installed jm doesn't run" }
  Say "Installed $installed in $dir\bin"
  # doctor's warnings (PATH, the toolchain still to download) are for the user;
  # its errors mean this install won't work.
  Push-Location $dir
  & $jm doctor *>> $log
  $doctor = $LASTEXITCODE
  Pop-Location
  if ($doctor -ne 0) { Fail "jm doctor found a problem with this install" }

  $bin = Join-Path $dir 'bin'
  $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
  if (($userPath -split ';') -notcontains $bin) {
    [Environment]::SetEnvironmentVariable('Path', (@($bin) + ($userPath -split ';' | Where-Object { $_ })) -join ';', 'User')
    Say "Added $bin to your PATH: open a new terminal to use jm"
  }
  $env:Path = "$bin;$env:Path"
  Write-Host "Next: jm setup (connects your agent apps), then jm init in a new folder, or jm editor"
  foreach ($b in $backups) { Remove-Item -Recurse -Force $b[1] -ErrorAction SilentlyContinue }
} catch {
  $err = "install: $_"
  foreach ($b in $backups) {
    Remove-Item -Recurse -Force $b[0] -ErrorAction SilentlyContinue
    Move-Item $b[1] $b[0] -ErrorAction SilentlyContinue
  }
  Add-Content -Path $log -Value $err
  [Console]::Error.WriteLine("--- ${log}:")
  [Console]::Error.WriteLine((Get-Content -Raw $log))
  throw $err
} finally {
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
