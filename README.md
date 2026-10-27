# Notification Tweaks

**Version 1.0.0**

SKSE plugin for Skyrim SE/AE that takes over the small HUD notifications
(the lines `Debug.Notification`, notification-style messages and other
plugins put in the top left corner) and draws them itself through
[SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352).

- **No queue delay:** several notifications appear at once instead of one
  after another.
- **Free placement:** drag the block anywhere on screen, or set it with
  sliders; left, centered or right alignment.
- **Style:** font from the Menu Framework, size (relative to 1080p, so it
  looks the same at any resolution), color, shadow or outline, optional
  background, display time and fades.
- **Merges split messages:** YPS Immersive Fashion splits long thoughts and
  narration into two notifications; they are shown as one line again, in
  the right order, in any language. Also a rule for Bimbos of Skyrim
  thoughts and an optional generic rule.
- **History:** the last 100 notifications of the session with time of day,
  colors and a filter, on its own menu page.
- **Waits for menus:** lines that arrive while the inventory, map or another
  full-screen menu is open appear when it closes (optional). Lines sent
  right before a loading screen show up after it.
- Identical repeats stack as "(x2)". `$Key` notifications are translated
  like Vanilla does. Quest, skill and level messages stay Vanilla.
- Save-neutral: nothing is written to the save. Settings live in
  `Data/SKSE/Plugins/NotificationTweaks.ini`.
- English and German UI; more languages through
  `Interface/Translations/NotificationTweaks_<LANGUAGE>.txt`.

Turning "Replace Vanilla notifications" off hands the lines back to the
Vanilla HUD at once (the history keeps recording).

## Requirements

- Skyrim SE/AE (Address Library build; developed and tested on 1.7.104)
- SKSE64, Address Library for SKSE Plugins
- SKSE Menu Framework 3.x (without it the plugin stays inactive and the
  Vanilla HUD keeps the notifications)

## Layout

- `src/` plugin source: `Capture.*` hooks and line store, `Merge.*` merge
  rules, `Markup.*` htmlText subset, `Overlay.*` drawing and drag mode,
  `MenuIntegration.*` menu pages, `Settings.*` INI, `Translation.*` UI strings
- `extern/commonlibsse-ng/` CommonLibSSE-NG submodule (alandtse/CommonLibVR `ng`)
- `external/skse-menu-framework/` SKSE Menu Framework consumer header
- `config/Interface/Translations/` UI translation files (generated)
- `tools/` build, deploy, package and string-table scripts
- `docs/design-notes.md` how it works and why

## Build

```powershell
git submodule update --init --recursive
.\tools\build.ps1
```

Needs Visual Studio with the C++ workload, CMake, Ninja and vcpkg
(`VCPKG_ROOT`). Output: `build\release\NotificationTweaks.dll`.
`tools\package.ps1` builds the release archive in `dist\`;
`tools\deploy.ps1` is the author's helper that copies a build into an MO2
mod folder (`-Instance`, `-ModName`).

UI strings are edited in `tools/strings.py`; run `python tools/strings.py`
to regenerate `src/Translation.h` and the translation files.

Log: `My Games\Skyrim Special Edition\SKSE\NotificationTweaks.log`.

## License

GPL-3.0, see `LICENSE`. SKSE Menu Framework header: see
`external/skse-menu-framework/LICENSE`.
