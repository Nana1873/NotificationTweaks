# Single source for the UI strings: generates src/Translation.h, the
# LoadStrings() body in src/Translation.cpp and the UTF-16 translation files.
# Usage: python -I tools/strings.py
import pathlib
import re

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
    ("historyShowAll", "Show all hidden again", "Alle ausgeblendeten wieder anzeigen"),
    ("headAdvanced", "Advanced", "Erweitert"),
    ("enabled", "Replace Vanilla notifications", "Vanilla-Benachrichtigungen ersetzen"),
    ("enabledTip", "Off: the Vanilla HUD shows notifications again.", "Aus: Das Vanilla-HUD zeigt die Benachrichtigungen wieder selbst an."),
    ("headPosition", "Position", "Position"),
    ("dragButton", "Drag on screen...", "Auf dem Bildschirm ziehen..."),
    ("dragTip", "Closes this menu and shows sample lines. Drag them with the left mouse button, then click Done.", "Schließt dieses Menü und zeigt Beispielzeilen. Mit gedrückter linker Maustaste ziehen, dann auf Fertig klicken."),
    ("posX", "Horizontal", "Horizontal"),
    ("posY", "Vertical", "Vertikal"),
    ("align", "Alignment", "Ausrichtung"),
    ("alignLeft", "Left", "Links"),
    ("alignCenter", "Center", "Mitte"),
    ("alignRight", "Right", "Rechts"),
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
    ("test1", "Send test message", "Testmeldung senden"),
    ("testMany", "Send 4 at once", "4 gleichzeitig senden"),
    ("resetDisplay", "Reset display settings", "Anzeige zurücksetzen"),
    ("resetConfirm", "Confirm?", "Wirklich?"),
    ("footer", "Changes apply immediately and are saved.", "Änderungen wirken sofort und werden gespeichert."),
    ("compatHint", "Some mods split one long message into two notifications. These rules put the halves back into one line.", "Manche Mods teilen eine lange Meldung in zwei Benachrichtigungen auf. Diese Regeln setzen die Hälften wieder zu einer Zeile zusammen."),
    ("mergeYps", "YPS Immersive Fashion: merge split thoughts", "YPS Immersive Fashion: geteilte Gedanken zusammenführen"),
    ("mergeYpsTip", "Recognizes the exact split position YPS uses for thoughts longer than its limit (80 characters by default).", "Erkennt die genaue Trennstelle, die YPS für Gedanken über seinem Limit (Standard 80 Zeichen) verwendet."),
    ("mergeBos", "Bimbos of Skyrim: merge split thoughts", "Bimbos of Skyrim: geteilte Gedanken zusammenführen"),
    ("mergeBosTip", "Pink thought lines (CustomComments.esp) whose first part does not end a sentence.", "Rosa Gedankenzeilen (CustomComments.esp), deren erster Teil keinen Satz beendet."),
    ("mergeGeneric", "All mods: merge obvious continuations", "Alle Mods: offensichtliche Fortsetzungen zusammenführen"),
    ("mergeGenericTip", "First line without a sentence end, second line starts lowercase. Can also join unrelated messages.", "Erste Zeile ohne Satzende, zweite beginnt mit Kleinbuchstaben. Kann auch unabhängige Meldungen verbinden."),
    ("mergeWindow", "Max. gap between halves", "Max. Abstand der Hälften"),
    ("testYps", "Test YPS split", "YPS-Teilung testen"),
    ("testBos", "Test BoS split", "BoS-Teilung testen"),
    ("sample1", "Notification Tweaks: sample line", "Notification Tweaks: Beispielzeile"),
    ("sample2", "Drag with the left mouse button", "Mit gedrückter linker Maustaste ziehen"),
    ("sample3", '<font color="#FF00FF">A colored line</font>', '<font color="#FF00FF">Eine farbige Zeile</font>'),
    ("posWindowTitle", "Notification position", "Position der Benachrichtigungen"),
    ("posWindowHint", "Drag the framed lines. The position is saved when you let go.", "Die umrahmten Zeilen ziehen. Gespeichert wird beim Loslassen."),
    ("done", "Done", "Fertig"),
    ("testText", "This is a test notification.", "Das ist eine Testbenachrichtigung."),
    ("testManyText", "Message", "Meldung"),
    ("testYpsText", "I love how my shoulder-length hair lets me switch effortlessly between casual and elegant hairstyles.", "Ich liebe, wie ich mit schulterlangen Haaren mühelos zwischen lässigen und eleganten Frisuren wechseln kann."),
    ("testBosFirst", "I can't stop thinking about", "Ich muss ständig daran denken,"),
    ("testBosSecond", "how pretty my nails look...", "wie hübsch meine Nägel aussehen..."),
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
(ROOT / "src/Translation.h").write_text("\n".join(header), encoding="utf-8")

body = ["    void LoadStrings()", "    {", "        Strings defaults;"]
for name, _, _ in STRINGS:
    body.append(f'        g_strings.{name} = Translate("{key(name)}", defaults.{name});')
body += ["        logs::info(\"UI strings loaded ('{}')\", g_strings.pageDisplay);", "    }", ""]
cpp_path = ROOT / "src/Translation.cpp"
cpp = cpp_path.read_text(encoding="utf-8")
cpp = re.sub(r"(@@LOADSTRINGS@@\n|    void LoadStrings\(\)\n    \{.*?\n    \}\n\n)", lambda m: "\n".join(body) + "\n", cpp, count=1, flags=re.S)
cpp_path.write_text(cpp, encoding="utf-8")

out = ROOT / "config/Interface/Translations"
out.mkdir(parents=True, exist_ok=True)
for lang, idx in (("ENGLISH", 1), ("GERMAN", 2)):
    lines = [f"{key(e[0])}\t{e[idx]}" for e in STRINGS]
    (out / f"NotificationTweaks_{lang}.txt").write_bytes(b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"))
print(f"{len(STRINGS)} strings written")
