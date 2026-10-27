# Configures and builds NotificationTweaks.dll from a VS x64 developer environment.
# Usage: .\tools\build.ps1 [-Preset release] [-ConfigureOnly]
# Full compiler output goes to build\build-log.txt.
[CmdletBinding()]
param(
    [string]$Preset = 'release',
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found: $vcvars" }
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = 'E:\vcpkg' }
$installer = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
if (Test-Path $installer) { $env:PATH = "$installer;$env:PATH" }

New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
$log = Join-Path $root 'build\build-log.txt'

$steps = "cmake --preset $Preset"
if (-not $ConfigureOnly) { $steps += " && cmake --build --preset $Preset" }
$cmd = "call `"$vcvars`" >nul && cd /d `"$root`" && ($steps) > `"$log`" 2>&1"
cmd.exe /d /c $cmd
$exit = $LASTEXITCODE
Get-Content $log -Tail 30
if ($exit -ne 0) { throw "Build failed with exit code $exit (see $log)" }
Write-Host "[build] OK: $root\build\$Preset\NotificationTweaks.dll"
