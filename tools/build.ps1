# Configures and builds NotificationTweaks.dll from a VS x64 developer environment,
# then runs the unit tests (tests\Tests.cpp); a failing check fails the build.
# Usage: .\tools\build.ps1 [-Preset release] [-ConfigureOnly]
# Full compiler output goes to build\build-log.txt.
[CmdletBinding()]
param(
    [string]$Preset = 'release',
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
# Newest Visual Studio with the C++ x64 tools, found through vswhere.
$installer = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$vswhere = Join-Path $installer 'vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio with the C++ workload' }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'No Visual Studio installation with the C++ x64 tools found' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found: $vcvars" }
# vcvars itself calls vswhere, which is not on PATH in a PowerShell-spawned cmd.
$env:PATH = "$installer;$env:PATH"
if (-not $env:VCPKG_ROOT) {
    if (Test-Path 'E:\vcpkg\scripts\buildsystems\vcpkg.cmake') { $env:VCPKG_ROOT = 'E:\vcpkg' }
    else { throw 'Set VCPKG_ROOT to your vcpkg checkout' }
}

New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
$log = Join-Path $root 'build\build-log.txt'

$steps = "cmake --preset $Preset"
if (-not $ConfigureOnly) { $steps += " && cmake --build --preset $Preset && build\$Preset\NotificationTweaksTests.exe" }
$cmd = "call `"$vcvars`" >nul && cd /d `"$root`" && ($steps) > `"$log`" 2>&1"
cmd.exe /d /c $cmd
$exit = $LASTEXITCODE
Get-Content $log -Tail 30
if ($exit -ne 0) { throw "Build failed with exit code $exit (see $log)" }
Write-Host "[build] OK: $root\build\$Preset\NotificationTweaks.dll"
