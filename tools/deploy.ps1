# Copies the built DLL, PDB and translation files into the SkyrimAE dev mod
# "houseCARL - Notification Tweaks Dev". With -Register the mod is also added
# (enabled) to profiles\Default\modlist.txt, right above Quick Audio Sliders
# Dev; that step refuses to run while MO2 is open (MO2 rewrites the file).
# Dev helper for the author's MO2 instance; pass -Instance and -ModName for
# another one (the modlist anchor falls back to the top of the list).
# Usage: .\tools\deploy.ps1 [-Preset release] [-Register] [-Instance <MO2 folder>] [-ModName <mod>]
[CmdletBinding()]
param(
    [string]$Preset = 'release',
    [switch]$Register,
    [string]$Instance = 'E:\Modlisten\SkyrimAE',
    [string]$ModName = 'houseCARL - Notification Tweaks Dev'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$instance = $Instance
$modName = $ModName
$mod = Join-Path $instance "mods\$modName"

if (Get-Process SkyrimSE -ErrorAction SilentlyContinue) { throw 'Skyrim is running; quit it before deploying (the DLL is locked).' }

$dll = Join-Path $root "build\$Preset\NotificationTweaks.dll"
$pdb = Join-Path $root "build\$Preset\NotificationTweaks.pdb"
if (-not (Test-Path $dll)) { throw "DLL not built: $dll (run tools\build.ps1 first)" }

$plugins = Join-Path $mod 'SKSE\Plugins'
$translations = Join-Path $mod 'Interface\Translations'
New-Item -ItemType Directory -Force -Path $plugins, $translations | Out-Null
Copy-Item $dll $plugins -Force
if (Test-Path $pdb) { Copy-Item $pdb $plugins -Force }
Copy-Item (Join-Path $root 'config\Interface\Translations\NotificationTweaks_*.txt') $translations -Force

# An existing INI inside the mod makes USVFS write the settings back here
# instead of into overwrite\. Never replace one that is already there.
$ini = Join-Path $plugins 'NotificationTweaks.ini'
if (-not (Test-Path $ini)) {
    Set-Content -Path $ini -Value '; Notification Tweaks settings, written by the in-game menu (SKSE Menu Framework).' -Encoding ascii
}

$version = (Get-Content (Join-Path $root 'vcpkg.json') -Raw | ConvertFrom-Json).'version-semver'
$meta = Join-Path $mod 'meta.ini'
if (Test-Path $meta) {
    # Keep the version MO2 shows in step with the build.
    $text = Get-Content $meta -Raw
    $updated = $text -replace '(?m)^version=.*$', "version=$version"
    if ($updated -ne $text) { Set-Content -Path $meta -Value $updated -Encoding ascii -NoNewline }
} else {
    @(
        '[General]'
        'gameName=skyrimse'
        'modid=0'
        "version=$version"
        'category=0'
        'comments="Eigenes SKSE-Plugin: ersetzt die Vanilla-HUD-Benachrichtigungen (Position, Sofortanzeige, YPS/BoS-Zusammenfuehrung); Einstellungen im SKSE Menu Framework"'
        ''
        '[installedFiles]'
        'size=0'
    ) | Set-Content -Path $meta -Encoding ascii
}
Write-Host "[deploy] Files copied to $mod"

if ($Register) {
    if (Get-Process ModOrganizer -ErrorAction SilentlyContinue) { throw 'MO2 is running; close it before registering the mod in modlist.txt.' }
    $modlist = Join-Path $instance 'profiles\Default\modlist.txt'
    $lines = [System.Collections.Generic.List[string]](Get-Content $modlist -Encoding UTF8)
    if ($lines | Where-Object { $_ -match "^[+-]$([regex]::Escape($modName))$" }) {
        Write-Host '[deploy] Already listed in modlist.txt'
    } else {
        Copy-Item $modlist "$modlist.bak-notificationtweaks" -Force
        $anchor = $lines.IndexOf('+houseCARL - Quick Audio Sliders Dev')
        if ($anchor -lt 0) { $anchor = 1 }
        $lines.Insert($anchor, "+$modName")
        [System.IO.File]::WriteAllLines($modlist, $lines, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "[deploy] Registered '+$modName' in modlist.txt (line $($anchor + 1)); backup: modlist.txt.bak-notificationtweaks"
    }
}
