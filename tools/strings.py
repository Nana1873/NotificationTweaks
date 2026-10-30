# Single source for the UI strings: generates src/Translation.h, the
# LoadStrings() body in src/Translation.cpp and the UTF-16 translation files.
# Usage: python -I tools/strings.py [--check]
# --check writes nothing and fails when a generated file is out of date.
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

STRINGS = [
    ("pageDisplay", "Display", "Anzeige"),
    ("pageCompat", "Compatibility", "Kompatibilität"),
    ("pageHistory", "History", "Verlauf"),
    ("historyHint", "The last 100 notifications of this session, newest first.", "Die letzten 100 Benachrichtigungen dieser Sitzung, neueste oben."),
    ("historyFilter", "Filter...", "Filtern..."),
    ("historyClear", "Clear", "Leeren"),
    ("historyEmpty", "No notifications yet.", "Noch keine Benachrichtigungen."),
    ("historyHide", "Hide this message in the HUD. It stays in the history; numbers match any number.", "Diese Meldung im HUD ausblenden. Sie bleibt im Verlauf; Zahlen passen auf jede Zahl."),
    ("historyUnhide", "Hidden in the HUD. Click to show it again.", "Im HUD ausgeblendet. Klicken, um sie wieder anzuzeigen."),
    ("historyHideOff", "Hiding works while \"Replace Vanilla notifications\" is on.", "Ausblenden wirkt, solange \"Vanilla-Benachrichtigungen ersetzen\" an ist."),
    ("historyShowAll", "Show all hidden again", "Alle ausgeblendeten wieder anzeigen"),
    ("historyTime", "Display time for this message (numbers match any number). Right-click: back to the general setting.", "Anzeigedauer für diese Meldung (Zahlen passen auf jede Zahl). Rechtsklick: zurück zur allgemeinen Einstellung."),
    ("historyTimeDefault", "Default", "Standard"),
    ("historyTimeOff", "Own display times work while \"Replace Vanilla notifications\" is on.", "Eigene Anzeigedauern wirken, solange \"Vanilla-Benachrichtigungen ersetzen\" an ist."),
    ("headAdvanced", "Advanced", "Erweitert"),
    ("enabled", "Replace Vanilla notifications", "Vanilla-Benachrichtigungen ersetzen"),
    ("enabledTip", "Off: the Vanilla HUD shows notifications again.", "Aus: Das Vanilla-HUD zeigt die Benachrichtigungen wieder selbst an."),
    ("dragButton", "Edit layout...", "Layout bearbeiten..."),
    ("dragTip", "Move and resize the notification lines and the big messages in the middle of the screen.", "Benachrichtigungszeilen und die großen Meldungen in der Mitte verschieben und skalieren."),
    ("centerLocation", "Locations", "Orte"),
    ("centerQuest", "Quests", "Quests"),
    ("centerSkill", "Skills and level", "Skills und Stufe"),
    ("centerShout", "Words of power", "Worte der Macht"),
    ("newestOnTop", "Newest line on top", "Neueste Zeile oben"),
    ("showInMenus", "Show in menus", "In Menüs anzeigen"),
    ("showInMenusTip", "Off: while the inventory, map or another full-screen menu is open, notifications wait and appear when it closes.", "Aus: Solange Inventar, Karte oder ein anderes Vollbild-Menü offen ist, warten Meldungen und erscheinen nach dem Schließen."),
    ("maxWidth", "Max. line width", "Max. Zeilenbreite"),
    ("maxVisible", "Max. visible messages", "Max. sichtbare Meldungen"),
    ("headStyle", "Font and style", "Schrift und Stil"),
    ("font", "Font", "Schrift"),
    ("fontDefault", "Menu Framework default", "Standard des Menu Framework"),
    ("fontSize", "Font size", "Schriftgröße"),
    ("fontSizeTip", "Size at 1080p; other resolutions scale with the screen height.", "Größe bei 1080p; andere Auflösungen skalieren mit der Bildschirmhöhe."),
    ("lineSpacing", "Line spacing", "Zeilenabstand"),
    ("textColor", "Text color", "Textfarbe"),
    ("effect", "Text effect", "Texteffekt"),
    ("effectNone", "None", "Keiner"),
    ("effectShadow", "Shadow", "Schatten"),
    ("effectOutline", "Outline", "Kontur"),
    ("background", "Background opacity", "Hintergrund-Deckkraft"),
    ("headTiming", "Timing", "Zeitverhalten"),
    ("immediate", "Show at once (no delay)", "Sofort anzeigen (kein Delay)"),
    ("immediateTip", "Off: one new line per delay step, like the Vanilla queue.", "Aus: pro Verzögerungsschritt eine neue Zeile, wie die Vanilla-Warteschlange."),
    ("queueDelay", "Queue delay", "Verzögerung pro Zeile"),
    ("duration", "Display time", "Anzeigedauer"),
    ("fadeIn", "Fade in", "Einblenden"),
    ("fadeOut", "Fade out", "Ausblenden"),
    ("stack", "Stack identical messages (x2)", "Gleiche Meldungen zusammenfassen (x2)"),
    ("headTest", "Test", "Test"),
    ("headMerge", "Split messages", "Geteilte Meldungen"),
    ("headIconFonts", "Icon fonts", "Icon-Fonts"),
    ("test1", "Send test message", "Testmeldung senden"),
    ("testMany", "Send 4 at once", "4 gleichzeitig senden"),
    ("resetDisplay", "Reset display settings", "Anzeige zurücksetzen"),
    ("resetConfirm", "Confirm?", "Wirklich?"),
    ("footer", "Changes apply immediately and are saved.", "Änderungen wirken sofort und werden gespeichert."),
    ("compatHint", "Some mods split one long message into two notifications. These rules put the halves back into one line.", "Manche Mods teilen eine lange Meldung in zwei Benachrichtigungen auf. Diese Regeln setzen die Hälften wieder zu einer Zeile zusammen."),
    ("mergeYps", "YPS: merge split thoughts", "YPS: geteilte Gedanken zusammenführen"),
    ("mergeYpsTip", "Recognizes the exact split position YPS uses for thoughts longer than its limit (80 characters by default).", "Erkennt die genaue Trennstelle, die YPS für Gedanken über seinem Limit (Standard 80 Zeichen) verwendet."),
    ("mergeBos", "BoS: merge split thoughts", "BoS: geteilte Gedanken zusammenführen"),
    ("mergeBosTip", "Pink thought lines (CustomComments.esp) whose first part does not end a sentence.", "Rosa Gedankenzeilen (CustomComments.esp), deren erster Teil keinen Satz beendet."),
    ("mergeGeneric", "All mods: merge obvious continuations", "Alle Mods: offensichtliche Fortsetzungen zusammenführen"),
    ("mergeGenericTip", "First line without a sentence end, second line starts lowercase. Can also join unrelated messages.", "Erste Zeile ohne Satzende, zweite beginnt mit Kleinbuchstaben. Kann auch unabhängige Meldungen verbinden."),
    ("mergeWindow", "Max. gap between halves", "Max. Abstand der Hälften"),
    ("drawIconFonts", "Draw icon fonts (e.g. Oblivion Interaction Icons)", "Icon-Fonts zeichnen (z. B. Oblivion Interaction Icons)"),
    ("drawIconFontsTip", "Some mods put icons from their own font into messages, like the OII save icon. On: the plugin reads that font and draws the icons with the lines. Off: icons are left out; a message that is only an icon goes to the Vanilla HUD.", "Manche Mods setzen Icons aus ihrem eigenen Font in Meldungen, etwa das Speichern-Icon von OII. An: Das Plugin liest diesen Font und zeichnet die Icons mit den Zeilen. Aus: Icons fallen weg; eine Meldung, die nur aus einem Icon besteht, geht an das Vanilla-HUD."),
    ("iconFontsFound", "Icon fonts found:", "Gefundene Icon-Fonts:"),
    ("iconFontsNone", "No icon font found.", "Kein Icon-Font gefunden."),
    ("iconFontGlyphs", "icons", "Icons"),
    ("historyIconOnly", "(icon)", "(Icon)"),
    ("sample1", "Notification Tweaks: sample line", "Notification Tweaks: Beispielzeile"),
    ("sample2", "Drag with the left mouse button", "Mit gedrückter linker Maustaste ziehen"),
    ("posWindowTitle", "Layout editor", "Layout-Editor"),
    ("posWindowHint", "Drag a frame to move it, arrow keys for fine steps (Shift: larger), mouse wheel resizes it, right-click puts it back.", "Rahmen ziehen zum Verschieben, Pfeiltasten für feine Schritte (Shift: größer), Mausrad für die Größe, Rechtsklick setzt zurück."),
    ("done", "Done", "Fertig"),
    ("testText", "This is a test notification.", "Das ist eine Testbenachrichtigung."),
    ("testManyText", "Message", "Meldung"),
]


def key(name):
    return "$NT_" + name[0].upper() + name[1:]


def cpp_escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


assert "—" not in "".join(e + d for _, e, d in STRINGS), "no em dashes"

header = [
    "#pragma once",
    "",
    "// Generated by tools/strings.py; edit the table there.",
    "namespace NT::Translation",
    "{",
    "    // Parses Interface/Translations/<a_name>_<sLanguage>.txt (ENGLISH as",
    "    // fallback) into the plugin's own table; the engine's translation table",
    "    // is never written (writing it during startup can crash). Game thread,",
    "    // kDataLoaded.",
    "    void Import(std::string_view a_name);",
    "",
    "    std::string Translate(std::string_view a_key, std::string_view a_fallback = {});",
    "",
    "    struct Strings",
    "    {",
]
for name, en, _ in STRINGS:
    header.append(f'        std::string {name} = "{cpp_escape(en)}";')
header += ["    };", "", "    void           LoadStrings();", "    const Strings& S();", "}", ""]
outputs = {}  # path -> (bytes, compare as text)
outputs[ROOT / "src/Translation.h"] = ("\n".join(header).encode("utf-8"), True)

body = ["    void LoadStrings()", "    {", "        Strings defaults;"]
for name, _, _ in STRINGS:
    body.append(f'        g_strings.{name} = Translate("{key(name)}", defaults.{name});')
body += ["        logs::info(\"UI strings loaded ('{}')\", g_strings.pageDisplay);", "    }", ""]
cpp_path = ROOT / "src/Translation.cpp"
cpp = cpp_path.read_text(encoding="utf-8")
cpp, replaced = re.subn(r"(@@LOADSTRINGS@@\n|    void LoadStrings\(\)\n    \{.*?\n    \}\n\n)", lambda m: "\n".join(body) + "\n", cpp, count=1, flags=re.S)
assert replaced == 1, "LoadStrings() body not found in src/Translation.cpp"
outputs[cpp_path] = (cpp.encode("utf-8"), True)

out = ROOT / "config/Interface/Translations"
for lang, idx in (("ENGLISH", 1), ("GERMAN", 2)):
    lines = [f"{key(e[0])}\t{e[idx]}" for e in STRINGS]
    outputs[out / f"NotificationTweaks_{lang}.txt"] = (b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"), False)

if "--check" in sys.argv:
    # Generated files must match the table (package.ps1 runs this).
    stale = []
    for path, (data, text) in outputs.items():
        current = path.read_bytes() if path.exists() else b""
        if text:
            current, data = current.replace(b"\r\n", b"\n"), data.replace(b"\r\n", b"\n")
        if current != data:
            stale.append(str(path.relative_to(ROOT)))
    if stale:
        print("Out of date, run python -I tools/strings.py: " + ", ".join(stale))
        sys.exit(1)
    print(f"{len(STRINGS)} strings, generated files up to date")
    sys.exit(0)

out.mkdir(parents=True, exist_ok=True)
for path, (data, text) in outputs.items():
    # Unchanged files keep their timestamp (package.ps1 compares it with the DLL).
    current = path.read_bytes() if path.exists() else None
    if current is not None and (current.replace(b"\r\n", b"\n") == data.replace(b"\r\n", b"\n") if text else current == data):
        continue
    if text:
        path.write_text(data.decode("utf-8"), encoding="utf-8")
    else:
        path.write_bytes(data)
print(f"{len(STRINGS)} strings written")
