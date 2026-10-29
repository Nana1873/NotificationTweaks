# Notification Tweaks

**Version 1.1.0**

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
- **History:** the last 100 notifications of the session with time of day,
  colors and a filter, on its own menu page. The eye in front of a line
  hides that message in the HUD from then on; it still shows up in the
  history (grey, closed eye). Numbers count as any number, so hiding
  "25 Gold added" also hides "30 Gold added". Click the eye again, or use
  "Show all hidden again", to bring them back. Saved in the INI under
  `[Hidden]`.
- **Waits for menus:** lines that arrive while the inventory, map or another
  full-screen menu is open appear when it closes (optional). Lines sent
  right before a loading screen show up after it.
- Compatibility page: merges messages that some mods split into two
  notifications (YPS and BoS thoughts, optional generic rule).
- Identical repeats stack as "(x2)". `$Key` notifications are translated
  like Vanilla does. Quest, skill and level messages stay Vanilla.
- Icon fonts (e.g. Oblivion Interaction Icons' `$Iconographia`) cannot be
  drawn by the overlay; their glyphs are left out instead of showing the
  letter they are mapped to. A line that is only icons goes to the Vanilla
  HUD, which draws the real icon, or is hidden with the Compatibility
  option "Hide icon-only messages" (`HideIconOnly`).
- Save-neutral: nothing is written to the save. Settings live in
  `Data/SKSE/Plugins/NotificationTweaks.ini`. Two keys exist only there:
  `[General] VerboseLog=1` logs every notification and merge, and
  `[Compatibility] YPSLimit` (default 80, 30 to 1000) must match YPS's own
  thought limit if you changed it in its MCM.
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
(`VCPKG_ROOT`). Output: `build\release\NotificationTweaks.dll`; the build
also runs the unit tests in `tests\` and fails if one does.
`tools\package.ps1` builds the release archive in `dist\`;
`tools\deploy.ps1` is the author's helper that copies a build into an MO2
mod folder (`-Instance`, `-ModName`).

UI strings are edited in `tools/strings.py`; run `python -I tools/strings.py`
to regenerate `src/Translation.h`, the `LoadStrings()` body in
`src/Translation.cpp` and the translation files.

Log: `My Games\Skyrim Special Edition\SKSE\NotificationTweaks.log`.

## License

GPL-3.0, see `LICENSE`. SKSE Menu Framework header: see
`external/skse-menu-framework/LICENSE`.
