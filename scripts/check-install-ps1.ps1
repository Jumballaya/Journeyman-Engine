# install.ps1 run the way the README says (a scriptblock, -Editor) against fake
# offline releases: upgrades, failed upgrades roll back, every failure is logged.
$ErrorActionPreference = 'Stop'
$installer = Join-Path $PSScriptRoot 'install.ps1'
$work = Join-Path ([IO.Path]::GetTempPath()) ('jm-install-test-' + [Guid]::NewGuid())
$release = Join-Path $work 'release'
$dir = Join-Path $work 'jm'

# A jm.exe whose doctor fails while JM_FAKE_DOCTOR_FAIL is set.
New-Item -ItemType Directory -Path (Join-Path $work 'fake') | Out-Null
Set-Content (Join-Path $work 'fake\main.go') @'
package main

import ("fmt"; "os")

func main() {
	if len(os.Args) > 1 && os.Args[1] == "doctor" && os.Getenv("JM_FAKE_DOCTOR_FAIL") != "" {
		os.Exit(1)
	}
	fmt.Println("jm fake")
}
'@
$fakeJm = Join-Path $work 'fake\jm.exe'
go build -o $fakeJm (Join-Path $work 'fake\main.go')
if ($LASTEXITCODE -ne 0) { throw 'go build failed' }

# Release makes a CLI and an editor zip whose version.txt says $version.
function Release($version, [switch]$BadZip) {
  Remove-Item -Recurse -Force $release, (Join-Path $work 'stage') -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Path $release | Out-Null
  foreach ($kind in 'cli', 'editor') {
    $name = "journeyman-$kind-windows-amd64"
    $stage = Join-Path $work "stage\$name"
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item $fakeJm (Join-Path $stage 'jm.exe')
    Set-Content (Join-Path $stage 'version.txt') $version
    Compress-Archive $stage (Join-Path $release "$name.zip")
  }
  if ($BadZip) { Set-Content (Join-Path $release 'journeyman-cli-windows-amd64.zip') 'not a zip' }
  $sums = Get-ChildItem $release -Filter *.zip | ForEach-Object { "$((Get-FileHash $_.FullName).Hash.ToLower())  $($_.Name)" }
  Set-Content (Join-Path $release 'SHA256SUMS') $sums
}

function Install {
  $env:JM_FROM = $release
  $env:JM_INSTALL_DIR = $dir
  try { & ([scriptblock]::Create((Get-Content -Raw $installer))) -Editor; $true } catch { Write-Host "(threw: $_)"; $false }
}
function Check($ok, $what) { if (-not $ok) { throw "FAIL: $what" }; Write-Host "ok: $what" }
function Installed($version) { (Get-Content (Join-Path $dir 'bin\version.txt')) -eq $version -and (Get-Content (Join-Path $dir 'editor\version.txt')) -eq $version }
function NoBackups { @(Get-ChildItem $dir -Filter '*.old-*').Count -eq 0 }

Release 'old'
Check (Install) 'fresh install'
Release 'new'
Check (Install) 'upgrade'
Check (Installed 'new') 'upgrade replaces bin and editor'
Check (NoBackups) 'upgrade removes the old folders'

Release 'newer'
$env:JM_FAKE_DOCTOR_FAIL = '1'
Check (-not (Install)) 'install fails when doctor fails'
$env:JM_FAKE_DOCTOR_FAIL = $null
Check (Installed 'new') 'failed upgrade restores bin and editor'
Check (NoBackups) 'failed upgrade leaves no old folders'

Release 'bad' -BadZip
Check (-not (Install)) 'install fails on a damaged zip'
Check ((Get-Content -Raw (Join-Path $dir 'install.log')) -match 'install: ') 'the damaged zip error is in install.log'
Check (Installed 'new') 'damaged zip leaves the install as it was'

Remove-Item -Recurse -Force $work
