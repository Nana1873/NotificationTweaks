# Packages the release archive for Nexus from the built DLL and the
# translation files. No .pdb ships. Output: dist\NotificationTweaks-<version>.zip
# Usage: .\tools\package.ps1 [-Preset release]
[CmdletBinding()]
param(
    [string]$Preset = 'release'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$version = (Get-Content (Join-Path $root 'vcpkg.json') -Raw | ConvertFrom-Json).'version-semver'
if (-not $version) { throw "No version-semver in vcpkg.json" }

$cmakeVersion = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(\S+ VERSION (\S+)').Groups[1].Value
# CMake versions have no prerelease tag, so compare the numeric core.
if ($cmakeVersion -ne ($version -replace '-.*$', '')) { throw "Version mismatch: vcpkg.json $version, CMakeLists.txt $cmakeVersion" }

& python -I (Join-Path $root 'tools\strings.py') --check
if ($LASTEXITCODE -ne 0) { throw 'Generated string files are out of date (run python -I tools\strings.py)' }

$dll = Join-Path $root "build\$Preset\NotificationTweaks.dll"
if (-not (Test-Path $dll)) { throw "DLL not built: $dll (run tools\build.ps1 first)" }
$newestSource = Get-ChildItem (Join-Path $root 'src') -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($newestSource.LastWriteTime -gt (Get-Item $dll).LastWriteTime) {
    throw "DLL is older than src\$($newestSource.Name) (run tools\build.ps1 first)"
}

# build.ps1 links the DLL before the tests run, so a failed test still
# leaves a fresh DLL behind: run them again here.
$tests = Join-Path $root "build\$Preset\NotificationTweaksTests.exe"
if (-not (Test-Path $tests)) { throw "Unit tests not built: $tests (run tools\build.ps1 first)" }
& $tests
if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed' }

$translations = Join-Path $root 'config\Interface\Translations'
if (-not (Test-Path (Join-Path $translations 'NotificationTweaks_ENGLISH.txt'))) {
    throw "Translation files missing under $translations"
}

$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'SKSE\Plugins') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'Interface\Translations') | Out-Null

Copy-Item $dll (Join-Path $stage 'SKSE\Plugins\NotificationTweaks.dll')
Copy-Item (Join-Path $translations 'NotificationTweaks_ENGLISH.txt') (Join-Path $stage 'Interface\Translations\')
Copy-Item (Join-Path $translations 'NotificationTweaks_GERMAN.txt') (Join-Path $stage 'Interface\Translations\')
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'NotificationTweaks-LICENSE.txt')
Copy-Item (Join-Path $root 'config\THIRD-PARTY-NOTICES.txt') (Join-Path $stage 'NotificationTweaks-THIRD-PARTY-NOTICES.txt')

$archive = Join-Path $dist "NotificationTweaks-$version.zip"
if (Test-Path $archive) { Remove-Item $archive -Force }
# tar.exe (bsdtar, part of Windows 10+) writes zip entries with "/" as the
# spec requires; Compress-Archive in PowerShell 5.1 writes "\".
$entries = Get-ChildItem $stage | ForEach-Object { $_.Name }
& tar.exe -a -c -f $archive -C $stage @entries
if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
Remove-Item $stage -Recurse -Force

Write-Host "[package] OK: $archive"
Get-Item $archive | Select-Object Name, Length
