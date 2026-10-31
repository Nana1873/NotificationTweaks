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
  the "delay") never sees it (except in the fallback below). Disabled, the
  line is only recorded in the history and passed on.
- Hook order and other mods (1.3.1): both hooks are written in
  `SKSEPlugin_Load` and stay inert until `Activate()` at `kDataLoaded`
  (only with SKSE Menu Framework present). In a vtable chain the hook
  written last runs first, so a mod hooking `HUDMenu::ProcessMessage`
  later wraps around ours: ChatBox (Nexus 169429) hooks at data load,
  calls its original (us) first and then reads the `HUDData` text, so it
  gets every line while Vanilla does not. Fallback when a DLL hooked even
  before us (the slot then points outside the game module, logged as
  "already hooked by"): a taken line is passed on to that hook, and the
  entry Vanilla adds to the HUD movie's queue
  (`HUDMovieBaseInstance.MessagesInstance.MessageArray`, a plain AS array
  of strings, shown on a later frame) is removed right after: the entry
  the call added, whatever that mod made of the text. A HUD without that
  array logs a warning once; lines may then show in Vanilla too. Vanilla
  never routes these lines through `HUDNotifications::ProcessMessage` or
  its native queue; both were tried.
- `HUDNotifications::Update` (vfunc 0x1) is a second net: plain
  notification entries (no quest, no word of power) are taken out of the
  queue before Vanilla shows them. In every test so far all lines came
  through the first hook; the net never logged.
- Quest, skill, level and other `HUDNotifications` types keep their Vanilla
  look (they can be moved and resized, see Center notifications).
- Sounds: `HUDData` has no sound field, so a sound passed to
  `ShowHUDMessage` cannot ride the first path. Queue entries do carry one;
  the Update hook plays it before erasing the entry.
- Threads: the hooks run on the game thread, overlay and menu on the
  framework's render callback. The game thread never reads `Settings`; it
  reads `CaptureSettings`, a copy published under a lock by
  `LoadSettings`/`SaveSettings`. Entries and history share `g_lock`; the
  overlay and the history page copy them out before drawing.
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
  mid-sentence and whose second half starts lowercase (the ~500 ticker
  narration lines carry actor names and are in no database). Without the
  lowercase check, load-time bursts merged wrongly in game ("Zuschauer-
  Vertreiber - Ziel hinzugefügt" + "... - Bereich hinzugefügt").
- **BoS**: Bimbos of Skyrim 1.9.1.2 never splits text itself (every
  compiled script in the instance was searched); each thought is one
  `Debug.Notification("<font color=\"#FF00FF\">...</font>")`. The rule
  merges two lines in that wrapping color when the first does not end a
  sentence, for the case that something else splits them. Reports of
  "split BoS thoughts" so far were YPS thoughts (example: thought 01680,
  "Ich brauch eine Begleitung, die meine Sachen | trägt, ...").
- **Generic** (off by default): no sentence end + lowercase continuation.
- **Repeats** (not a merge rule): a repeat stacks as "(x2)" only onto the
  newest shown line, so Enabled, Disabled, Enabled stays three lines
  instead of the oldest line reading as the current state. The engine's
  ShowHUDMessage (SE 52050 / AE 52933) also drops a line sent with
  `cancelIfAlreadyQueued` (Debug.Notification, most plugins) whose text
  hash is among the last eight of the past 5 s; Auto Nearby Quest Tracker
  lost its newest state that way. The plugin clears that table (8 x {u32
  hash, u32 ms}, checked at startup against the static-init guard right
  behind it) after each line it takes, unless stacking is off. Cleared on:
  SE 1.5.97 (table 520091, guard 520093; read from the running code in the
  SE test instance, the SE exe is encrypted on disk); the AE versions listed
  in `kRepeatFilterVersionsAE` (1.6.317 to 1.6.1179, 1.7.99, 1.7.104;
  406616, 406624). Other SE versions, VR and
  newer runtimes keep the filter.
  Trade-offs: interleaved repeats (A, B, A, B) now each get a line, and a
  script sending A, B, A within one frame still loses the second A (the
  table is cleared on the main thread after the sender recorded it; a hook
  on ShowHUDMessage forcing `cancelIfAlreadyQueued` off would close that).
  Shown and hidden lines stack separately, so a hidden spammer stays one
  entry.

## Overlay (src/Overlay.cpp)

- Drawn on `GetBackgroundDrawList()` from an SMF `HudElement`, so SMF
  windows cover it. Text parsed from the Scaleform htmlText subset
  (`<font color>`, `<br>`, entities); invalid UTF-8 is read as
  Windows-1252.
- Fonts come from the framework's ImGui atlas (default: its Futura
  Condensed, extracted from the game's own font). Font size and line
  spacing are pixels at 1080p, scaled by screen height.
- Other alphabets (1.3.0): SMF loads one extra glyph set (its settings,
  Character Glyphs) into its fonts. A line whose letters the chosen font
  lacks (`ImFont_FindGlyphNoFallback`; ASCII, punctuation U+2000-206F and
  icon runs not checked) is drawn in the first atlas font that has them
  all (the framework's default font first; a candidate needs Latin
  letters, so icon fonts never qualify). Only that line switches, so a
  German font keeps its umlauts. The language fonts must not simply be
  chosen as the main font: SMF's RU font puts Cyrillic letters on the
  Latin-1 code points (ä shows as д). A line no font fits stays as it is
  ("?") and the Display page shows a hint where to set the glyphs. The
  hint is decided per frame: set when a line fits no font, cleared when
  lines needed another font and all found one, kept otherwise (so it is
  still there after the line has faded).
- Layout editor without a gamepad path since 1.3.0: the position sliders
  were removed with the Layout page, so moving needs mouse and keyboard.
- Hidden while the HUD movie is invisible, at the main menu and during
  loading screens. With `ShowInMenus` off (default), lines wait while a
  full-screen menu is open and appear when it closes; those menus are detected by name because
  Skyrim Souls RE keeps them unpaused, so `GameIsPaused()` is false there.
  Dialogue is not on the list: lines show during conversations.
- Layout editor ("Edit layout..." on the Display page): an SMF window
  (`AddWindow`) with Done replaces the main window. A yellow frame around
  the lines and a labelled box per center group (see Center
  notifications) follow a left-button drag; the mouse wheel resizes (lines:
  font size; boxes: 5 % per notch), right-click puts it back. A drag saves
  on release, the wheel half a second after the last notch, right-click at
  once. The topmost frame under the mouse takes a gesture (center boxes
  above the lines), so one notch never changes two things. Arrow keys
  move the last clicked frame one pixel (Shift: ten); the window takes no
  keyboard navigation for that. The lines' alignment follows the block's
  center (left third left, right third right, else centered), so the
  block grows away from the nearer screen edge; there is no separate
  alignment setting in the UI. The window pauses the game, so nothing
  that needs the HUD to run (a sample popup) can be shown there; an
  earlier preview button was dropped for that reason.

## Settings and strings

- `Data/SKSE/Plugins/NotificationTweaks.ini`; the dev mod ships an empty
  one so USVFS writes back into the mod instead of overwrite.
- `$Key` notifications are translated through our table, then only the
  translator's virtual `Translate()`. Never read `translationMap` (or any
  translator member) directly: ScaleformTranslationPP installs its own
  translator, and that read crashed 1.0.0 on SE 1.5.97 for a key nobody
  translates.
- Translations are parsed into the plugin's own table (never
  `SKSE::Translation::ParseTranslation`, which crashed Quick Audio Sliders
  1.0.0 by writing the engine table during startup). English defaults are
  compiled in; `tools/strings.py` generates `Translation.h`, the
  `LoadStrings()` body and both UTF-16 translation files.
- History: last 100 lines with real time, memory only (save-neutral).

## Icon fonts (1.1.0)

- Oblivion Interaction Icons sets GMSTs like sQuickSaving to
  `<font face='$Iconographia'> w </font>`; the face is mapped in
  `Interface\fontconfig.txt` (`map "$Iconographia" = "Iconographia"`) to a
  DefineFont3 in `Interface\Fonticon.swf` (CWS, zlib).
- `Markup::Parse` keeps text inside a face whose name contains "icon" or
  "symbol" as runs with `iconFace` set; `Visible()` leaves them out (merge
  rules, history and hide keys see text only).
- `SwfFont` (pure, unit-tested) parses fontconfig.txt, inflates the SWF,
  reads DefineFont2/3 glyph shapes (curves flattened, EM units) and
  rasterizes them even-odd. Each shape is read only within its own slice
  of the shape table; glyph count, points per glyph and atlas height are
  capped, and loading runs in try/catch (a bad file means no icons, never
  a crash).
- `IconFont::Preload` loads every icon face fontconfig.txt maps at
  kDataLoaded (and again when "Draw icon fonts" is switched on in the
  menu after starting with it off) (about 0.3 s with OII and the vanilla `$SkyrimSymbolsFont`
  from fonts_en.swf), so nothing is read during play. Glyphs are packed
  into one white RGBA atlas (alpha = coverage) at 96, 48 and 24 px; the
  overlay samples the level closest above its line height, because the
  menu framework's sampler has no mipmaps. The D3D11 texture is created
  on the render thread on first draw; if that fails, `Has()` turns false
  and such lines go back to Vanilla. Glyph boxes include outline parts
  outside the advance. The overlay draws each glyph with `AddImage`, one
  line high, tinted like text, with the same shadow/outline.
- Nothing ships from OII: the font is read from the player's install.
- A line with nothing drawable (icon fonts off via `DrawIconFonts`, or
  the font missing) makes `Capture::Push` return false and goes back to
  the Vanilla HUD, which renders the icon itself, unless it is hidden
  (then Push swallows it). It is recorded in the history either way.
- The History page draws a line's icon glyphs before its text (one text
  line high, tinted like the line); "(icon)" only appears when the glyphs
  cannot be drawn. Icon-only lines get an eye too (see Hiding).
- Verified in game (AE 1.7.104, OII DSD 2.60): 60 drawable glyphs; save
  and load icons, colored icons and icon + text render at the overlay's
  position.

## Hiding (1.1.0)

- The eye on a History line hides by key: `Text::HideKey` turns the
  visible text (`Markup::Visible`) into a key with every ASCII digit run
  replaced by `#`, so "25 Gold added" and "30 Gold added" share one key.
  A line without text is keyed by its icon glyphs instead
  (`Markup::IconKey`, e.g. `[icon] $Iconographia:w`). A hidden line stays
  hidden even when it cannot be drawn (icon fonts off or missing):
  `Capture::Push` swallows it instead of handing it to Vanilla.
- Keys live in `[Hidden]` as `Count` plus `Line1..N`, each value quoted
  (the profile API strips outer quotes, keeping edge spaces) and
  `%XX`-escaped for bytes >= 0x80, < 0x20 and `%` (the A-API goes through
  the ANSI codepage, which breaks UTF-8 on DBCS locales). The section is
  written in one `WritePrivateProfileSectionA` call outside `g_lock`.
- Hidden lines stay in the live queue (so a second half can still merge)
  but are not drawn, do not count for MaxVisible and never hold up queue
  mode. A line that is shown again (eye, "show all" or a merge that changes
  its key) restarts: `started`/`age` reset and it moves to the back as the
  newest line.
- With "Replace Vanilla notifications" off the eye is disabled; the Vanilla
  HUD shows everything then.

## Own display time (1.2.0)

- A slider at the right edge of a History row sets the fully visible time
  for every line with the same hide key (`[Durations]`: `Count`, `LineN`
  escaped like `[Hidden]`, `TimeN` in seconds within `Limits::kDuration`).
  Fade in and fade out stay general.
- It is always there (a hover-only slider is out of reach with a gamepad)
  and dimmed while the line uses the general setting and its row is not
  hovered, so lines with their own time stand out. With "Replace Vanilla
  notifications" off it is disabled, like the eye. Right-click goes back
  to the general setting. The value applies to live lines at once; the
  file is written on release (or, if the row vanished mid-edit, on the
  next History frame or when the menu closes).
- `Entry::duration` is resolved when a line arrives or merges and when a
  time changes; the overlay's alpha, lifetime and MaxVisible fade and the
  stacking window use it instead of the general Duration.

## Center notifications (1.3.0)

- Location discovered (also Dynamic Location Pop-ups, which sends a plain
  `kLocationDiscovery` HUD message, see its `Manager::SendLocationPopup`),
  quests and objectives, skill and level increases, words of power and
  dragon souls all show in `HUDMovieBaseInstance.QuestUpdateBaseInstance`
  (Vanilla and Dragonborn UI `hudmenu.swf` alike), one entry at a time from
  the `HUDNotifications` queue, whose `root` is that clip.
- `CenterNotifications` (game thread, around the `HUDNotifications::Update`
  hook): when the front entry leaves the queue it starts showing; if it is
  a center type, the clip gets that group's offset (HUD units) and scale
  via `SetDisplayInfo`, relative to the HUD's own values. Plain lines that
  Vanilla still shows (capture off, undrawable icons) never touch the clip,
  so a popup on screen is not moved. The HUD's own values are read again
  whenever the clip is no longer where it was last set (a HUD mod moved it,
  the movie reloaded). Center entries never overlap, so groups can sit in
  different places. Types are mapped through `RE::GetHUDMessageType` (VR
  shifts them by 3). `[CenterNotifications]` holds `<Group>X/Y/Scale` for
  Location, Quest, Skill, Shout (`kCenterKeys`).
- The editor's boxes come from a geometry measured about once a second
  (`localToGlobal` of the clip origin through the movie's visible frame):
  origin in screen pixels, pixels per HUD unit on each axis and the HUD's
  own clip scale. `Geometry::TitleRect` estimates the title line from
  that; in game a popup lands inside the box.
- Test hook: with `VerboseLog=1` a notification `NTDEBUG:<type>:<text>`
  with the location type (`kLocationDiscovery`, 26 on SE/AE) becomes a
  location popup, sent like DLP does; other types are refused (they would
  need a quest or word of power).

## Testing notes

- Unit tests (`tests/Tests.cpp`, built and run by `tools/build.ps1` and
  again by `package.ps1`) cover the pure code: YPS/BoS/generic merge
  rules, markup parsing, entities and icon runs, the 1252 fallback, hide
  keys, the `[Hidden]` and ImGui format escaping, UTF-8 code points, fontconfig parsing,
  the even-odd rasterizer and the atlas level choice. They link
  `Merge.cpp`, `Markup.cpp` and `SwfFont.cpp` (with zlib) against
  `tests/TestPCH.h` (no CommonLibSSE, `logs::` is a no-op). Setting
  `NT_TEST_SWF` (path to a .swf) and `NT_TEST_FONT` (font name) also loads
  a real font library and writes its "w" glyph as `glyph_w.pgm`. Hooks,
  overlay, textures and menu stay in-game tests.
- Not unit-tested: the `[Durations]` / `WriteKeySection` round trip (the
  escaping it uses is), the engine repeat-filter table and the History
  slider. Checked in game on AE 1.7.104: three fast Auto Nearby Quest
  Tracker toggles all arrive, Papyrus A, B, A, A shows "A / B / A (x2)",
  the startup log has no "Engine repeat filter left alone" line there,
  and the slider (dimmed default, own time, right-click reset, saved on
  release, kept across a restart) works by hand. In the test instances
  (`instances/` at the lab root) SE 1.5.97 and AE 1.6.1170 log "Engine
  repeat filter cleared after each taken line" and Papyrus A, B, A (1 s
  apart) arrives as three lines; ANQT itself and the menu were not run
  there.
- Center notifications, checked in game (AE 1.7.104, Dragonborn UI HUD):
  a location popup and a quest objective right after it each at their own
  place and size; boxes drag, resize and reset; values survive a restart.
- `tools/deploy.ps1 [-Register]`, then
  `..\..\tools\agent\launch-skyrim-skse.ps1` and DevBench. Most tests need
  no save: `coc riverwood` (DevBench console) from the main menu avoids
  the missing-content dialogs. Two overlapping loads (a stale load script
  plus a manual one) crashed in `Main::ReloadFormData`. Loading the save
  by name only works once the data is loaded; the test save asks about
  missing `iWant Status Bars.esp` / `SLWidgets.esp` (answer Yes, never
  save); a Bethesda.net `LoginMenu` can block the load (close it).
- Notifications are sent with a DevBench `scenario` of `papyrus` calls to
  `Debug.Notification`, so halves arrive back to back like from a script.
  `Message.Show` needs explicit float args or `fillNeutral`.
- The running game keeps `build/release/NotificationTweaks.pdb` open
  (dbghelp follows the embedded path): quit the game before linking.
- The SMF cursor follows relative raw mouse input; driving it from outside
  with absolute moves is unreliable, so drag tests are manual. Clicks on
  menu items work with Windows-MCP absolute clicks on the game monitor, but
  ImGui may register a click at the previous cursor position, so click the
  same spot twice when one seems to be missed.
