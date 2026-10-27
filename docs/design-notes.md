# Notification Tweaks: design notes

SKSE plugin (CommonLibSSE-NG, alandtse `ng`, 13.1.0) that takes the plain
HUD notifications (`Debug.Notification`, `Message.Show` without a box,
`ShowHUDMessage` from other plugins) away from the Vanilla HUD and draws
them itself on the SKSE Menu Framework HUD layer. Verified on Skyrim SE
1.7.104 in the SkyrimAE instance (2026-10-27).

## Capture (src/Capture.cpp)

- `HUDMenu::ProcessMessage` (IMenu vfunc 0x4): a `kUpdate` whose data is a
  `HUDData` of type `kNotification` is one notification line. With the
  plugin enabled it is pushed to the overlay and the message is reported
  handled, so the Vanilla `HUDNotifications` queue (one line at a time,
  the "delay") never sees it. Disabled, the line is only recorded in the
  history and passed on.
- `HUDNotifications::Update` (vfunc 0x1) is a second net: plain
  notification entries (no quest, no word of power) are taken out of the
  queue before Vanilla shows them. In every test so far all lines came
  through the first hook; the net never logged.
- Quest, skill, level and other `HUDNotifications` types stay Vanilla.
- A notification that is exactly one `$Key` is translated through the
  Scaleform translator (read-only), like the Vanilla text field does
  (seen: `$Initialization_Start` from Racial Skin Variance).
- Pending lines are dropped at `kPreLoadGame` / `kNewGame`. Loading
  screens inside a session keep them: the overlay neither draws nor ages
  lines while `Loading Menu` is open or the HUD movie is hidden.

## Merge rules (src/Merge.cpp)

Both halves must arrive within `MergeWindow` (default 0.35 s; real
splits arrive 16 to 50 ms apart). Rules are checked in this order:

- **YPS**: `ypsThoughts.playthoughtchoice` and
  `YpsPiercingTicker.shownotification` split any text longer than
  LongThoughtLimit (80) at the first space at or after
  `max(30, len / 2 - 3)` (byte positions) and send
  `Substring(0, p)` / `Substring(p + 1)`. The formula alone also fits
  unrelated pairs of similar length (seen in game: SMP and RSV load lines,
  two BoS lines), so a pair merges only when the joined text is one of the
  thoughts read from `Data/SKSE/Plugins/ypsThoughts/*.json` at
  kDataLoaded (2713 with the German override; the `[nnnnn]` sound prefix
  is stripped), or when it is plain text whose first half ends
  mid-sentence (the ~500 ticker narration lines carry actor names and are
  in no database).
- **BoS**: Bimbos of Skyrim 1.9.1.2 never splits text itself (every
  compiled script in the instance was searched); each thought is one
  `Debug.Notification("<font color=\"#FF00FF\">...</font>")`. The rule
  merges two lines in that wrapping color when the first does not end a
  sentence, for the case that something else splits them. Reports of
  "split BoS thoughts" so far were YPS thoughts (example: thought 01680,
  "Ich brauch eine Begleitung, die meine Sachen | trägt, ...").
- **Generic** (off by default): no sentence end + lowercase continuation.

## Overlay (src/Overlay.cpp)

- Drawn on `GetBackgroundDrawList()` from an SMF `HudElement`, so SMF
  windows cover it. Text parsed from the Scaleform htmlText subset
  (`<font color>`, `<br>`, entities); invalid UTF-8 is read as
  Windows-1252.
- Fonts come from the framework's ImGui atlas (default: its Futura
  Condensed, extracted from the game's own font). Font size and line
  spacing are pixels at 1080p, scaled by screen height.
- Hidden while the HUD movie is invisible, at the main menu and during
  loading screens. `ShowInMenus` (default off) hides lines while a
  full-screen menu is open; those menus are detected by name because
  Skyrim Souls RE keeps them unpaused, so `GameIsPaused()` is false there.
  Dialogue is not on the list: lines show during conversations.
- Drag mode: an SMF window (`AddWindow`) with Done replaces the main
  window; the frame around the block follows the mouse and saves on
  release.

## Settings and strings

- `Data/SKSE/Plugins/NotificationTweaks.ini`; the dev mod ships an empty
  one so USVFS writes back into the mod instead of overwrite.
- Translations are parsed into the plugin's own table (never
  `SKSE::Translation::ParseTranslation`, which crashed Quick Audio Sliders
  1.0.0 by writing the engine table during startup). English defaults are
  compiled in; `tools/strings.py` generates `Translation.h`, the
  `LoadStrings()` body and both UTF-16 translation files.
- History: last 100 lines with real time, memory only (save-neutral).

## Testing notes

- `tools/deploy.ps1 [-Register]`, then
  `..\..\tools\agent\launch-skyrim-skse.ps1` and DevBench. Loading the save
  by name only works once the data is loaded; the test save asks about
  missing `iWant Status Bars.esp` / `SLWidgets.esp` (answer Yes, never
  save); a Bethesda.net `LoginMenu` can block the load (close it).
- Notifications are sent with a DevBench `scenario` of `papyrus` calls to
  `Debug.Notification`, so halves arrive back to back like from a script.
  `Message.Show` needs explicit float args or `fillNeutral`.
- The running game keeps `build/release/NotificationTweaks.pdb` open
  (dbghelp follows the embedded path): quit the game before linking.
- The SMF cursor follows relative raw mouse input; driving it from outside
  with absolute moves is unreliable, so drag tests are manual.
