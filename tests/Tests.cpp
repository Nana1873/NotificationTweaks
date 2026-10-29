// Unit tests for the pure parts of the plugin: merge rules, markup parsing,
// hide keys, INI escaping, fontconfig parsing, the SWF glyph rasterizer and
// atlas level choice. Built with the plugin; tools\build.ps1 runs
// the exe and fails the build when a check fails.

#include "Markup.h"
#include "Merge.h"
#include "SwfFont.h"
#include "Text.h"

namespace
{
    int g_failed = 0;
    int g_passed = 0;

    void Check(bool a_ok, std::string_view a_what, int a_line)
    {
        if (a_ok) {
            ++g_passed;
        } else {
            ++g_failed;
            std::cout << "FAIL line " << a_line << ": " << a_what << '\n';
        }
    }

#define CHECK(expr) Check((expr), #expr, __LINE__)

    void TestText()
    {
        using namespace NT::Text;
        CHECK(HideKey("25 Gold added") == "# Gold added");
        CHECK(HideKey("1200 Gold added") == "# Gold added");
        CHECK(HideKey("Iron Ore (3) added, 12/40") == "Iron Ore (#) added, #/#");
        CHECK(HideKey("no digits") == "no digits");
        CHECK(HideKey("") == "");
        // UTF-8 continuation bytes 0xB2/0xB3/0xB9 are no digits.
        CHECK(HideKey("\xC3\xB2\xC3\xB9 9") == "\xC3\xB2\xC3\xB9 #");

        CHECK(PercentEscape("# Gold hinzugef\xC3\xBCgt") == "# Gold hinzugef%C3%BCgt");
        CHECK(PercentEscape("100%") == "100%25");
        CHECK(PercentEscape("tab\there") == "tab%09here");
        for (const std::string_view sample : { "plain", "\xC3\xA4\xC3\xB6\xC3\xBC", "50% \"off\" = ; x", "%%%" }) {
            CHECK(PercentUnescape(PercentEscape(sample)) == sample);
        }
        CHECK(PercentUnescape("%") == "%");
        CHECK(PercentUnescape("%4") == "%4");
        CHECK(PercentUnescape("%4G") == "%4G");
        CHECK(PercentUnescape("a%41b") == "aAb");

        CHECK(LowerAscii("AbC\xC3\x84") == "abc\xC3\x84");  // ASCII only, UTF-8 bytes untouched
        CHECK(Trim("  \tx y\t ") == "x y");
        CHECK(Trim("   ").empty());
        CHECK(Lower("\xC3\x84" "BC") == "\xC3\xA4" "bc");

        std::string utf8;
        AppendUtf8(0x1F600, utf8);
        CHECK(utf8 == "\xF0\x9F\x98\x80");

        std::size_t       pos = 0;
        const std::string mixed = "w\xC3\xA4\xF0\x9F\x98\x80\xC3";
        CHECK(NextCodePoint(mixed, pos) == U'w');
        CHECK(NextCodePoint(mixed, pos) == U'ä');
        CHECK(NextCodePoint(mixed, pos) == U'\U0001F600');
        CHECK(NextCodePoint(mixed, pos) == U'\uFFFD' && pos == mixed.size());
    }

    void TestMarkup()
    {
        using namespace NT::Markup;
        CHECK(Visible("<font color='#FF00FF'>a &amp; b</font>") == "a & b");
        CHECK(Visible("one<br>two") == "one two");
        CHECK(Visible("&#x1F600;") == "\xF0\x9F\x98\x80");
        CHECK(Visible("&#xD800;") == "&#xD800;");  // a lone surrogate stays literal
        CHECK(Visible("&unknown;") == "&unknown;");

        CHECK(WrappingColor(" <font color=\"#FF00FF\">x</font> ") == 0xFF00FFu);
        CHECK(WrappingColor("<FONT COLOR=0x00FF00>x</FONT>") == 0x00FF00u);
        CHECK(!WrappingColor("<font bgcolor=\"#123456\">x</font>"));
        CHECK(!WrappingColor("<font color=\"#FF00FF\">x</font> tail"));

        CHECK(ToUtf8("\xE4") == "\xC3\xA4");             // Windows-1252 fallback
        CHECK(ToUtf8("\xC3\xA4") == "\xC3\xA4");         // valid UTF-8 stays

        // Icon fonts (Oblivion Interaction Icons, vanilla symbols): icon runs left out of Visible().
        CHECK(Visible("<font face=\"$Iconographia\">W</font> Saving...") == "Saving...");
        CHECK(Visible("<font face='$SkyrimSymbolsFont' color='#FFFFFF'>A</font>Text") == "Text");
        CHECK(Visible("<font face=\"$Iconographia\"><font color=\"#FF0000\">W</font></font> x") == "x");
        CHECK(Visible("<font face=\"$EverywhereFont\">Normal</font>") == "Normal");
        CHECK(Visible("<font face=\"$Iconographia\">W</font> Saving") == Visible("Saving"));
        const auto iconRuns = Parse("<font face='$Iconographia'> w </font>Saved");
        CHECK(IsIconFace("$Iconographia") && IsIconFace("$SkyrimSymbolsFont") && !IsIconFace("$EverywhereFont"));
        const auto saveIcons = Icons("<font face='$Iconographia'> w </font>");
        CHECK(saveIcons.size() == 1 && saveIcons[0].first == "$Iconographia" && saveIcons[0].second == U'w');
        CHECK(IconKey(saveIcons) == "[icon] $iconographia:w");
        CHECK(IconKey(Icons("plain text")).empty());
        const auto mixed = Icons("<font face='$Iconographia'>W</font> Saving");
        CHECK(mixed.size() == 1 && mixed[0].second == U'W');
        const auto several = Icons("<font face='$Iconographia'>ab</font> x <font face='$SkyrimSymbolsFont'>c</font>");
        CHECK(IconKey(several) == "[icon] $iconographia:a $iconographia:b $skyrimsymbolsfont:c");
        CHECK(Icons("<font face='$Iconographia'> \t </font>").empty());
        CHECK(IconKey({ { "$X", U'\u00E4' } }) == "[icon] $x:\xC3\xA4");

        // Hide keys: text wins (digits as #), icons only without text, else none.
        CHECK(LineHideKey("<font face='$Iconographia'>w</font> 25 Gold") == "# Gold");
        CHECK(LineHideKey("<font face='$Iconographia'> w </font>") == "[icon] $iconographia:w");
        CHECK(LineHideKey("<font face='$Iconographia'> </font>").empty());
        CHECK(iconRuns.size() == 2 && iconRuns[0].iconFace == "$Iconographia" && iconRuns[0].text == " w " && iconRuns[1].iconFace.empty());

        const auto runs = Parse("a <font color=\"#112233\">b</font> c");
        CHECK(runs.size() == 3);
        CHECK(runs.size() == 3 && runs[1].rgb == 0x112233u && !runs[0].rgb && !runs[2].rgb);
    }

    // Splits a_text the way ypsThoughts.psc does.
    std::pair<std::string, std::string> YpsSplit(const std::string& a_text)
    {
        const auto split = NT::Merge::YpsSplitPos(a_text);
        return { a_text.substr(0, split), a_text.substr(split + 1) };
    }

    void TestMerge()
    {
        using namespace NT::Merge;
        const std::string narration = "the wind pulls at her coat while she walks along the road towards the old mill and thinks of home";
        const auto [first, second] = YpsSplit(narration);
        CHECK(YpsSplitPos(narration) == first.size());
        CHECK(IsYpsSplit(first, second, 80));
        CHECK(!IsYpsSplit(first, second, 200));               // under the limit YPS never splits
        CHECK(!IsYpsSplit(first, "The" + second.substr(3), 80));  // uppercase start, no stored thought
        const auto earlier = first.rfind(' ');  // one word too early: not YPS's split
        CHECK(!IsYpsSplit(first.substr(0, earlier), first.substr(earlier + 1) + " " + second, 80));
        CHECK(!IsYpsSplit("short", "lowercase rest of it", 10));

        const auto pink = [](std::string_view a_text) { return std::format("<font color=\"#FF00FF\">{}</font>", a_text); };
        CHECK(IsBosContinuation(pink("I can't stop thinking about"), pink("how pretty my nails look...")));
        CHECK(!IsBosContinuation(pink("A whole sentence."), pink("Another one.")));
        CHECK(!IsBosContinuation("I can't stop thinking about", "how pretty my nails look..."));

        CHECK(IsGenericContinuation("You picked up the", "iron sword"));
        CHECK(!IsGenericContinuation("You picked up a sword.", "iron sword"));
        CHECK(!IsGenericContinuation("You picked up the", "Iron sword"));

        CHECK(Join(pink("a"), pink("b")) == pink("a b"));
        CHECK(Join(" left ", " right ") == "left right");
    }

    void TestSwfFont()
    {
        using namespace NT::SwfFont;
        const auto config = ParseFontConfig(
            "fontlib \"Interface\\fonts_en.swf\"\r\n  fontlib \"Interface\\Fonticon.swf\"\r\n"
            "map \"$EverywhereFont\" = \"Futura Condensed\" Normal\r\nmap \"$Iconographia\" = \"Iconographia\" Normal\r\n");
        CHECK(config.libs.size() == 2 && config.libs[1] == "Interface\\Fonticon.swf");
        CHECK(config.maps.size() == 2 && config.maps[1].first == "$Iconographia" && config.maps[1].second == "Iconographia");

        // A 4x4 square at (2,2) in a 8x8 bitmap: full inside, empty outside,
        // half coverage on a half-pixel edge.
        const std::vector<std::vector<Point>> square{ { { 2, 2 }, { 6, 2 }, { 6, 6 }, { 2, 6 } } };
        const auto                            full = Rasterize(square, 8, 8, 1.0F, 0.0F, 0.0F);
        CHECK(full[3 * 8 + 3] == 255 && full[0] == 0 && full[7 * 8 + 7] == 0);
        const auto half = Rasterize(square, 8, 8, 1.0F, 0.5F, 0.0F);
        CHECK(half[3 * 8 + 2] > 100 && half[3 * 8 + 2] < 160);
        // Even-odd: a square hole inside a square stays empty.
        const std::vector<std::vector<Point>> ring{ { { 0, 0 }, { 8, 0 }, { 8, 8 }, { 0, 8 } }, { { 3, 3 }, { 5, 3 }, { 5, 5 }, { 3, 5 } } };
        const auto                            hole = Rasterize(ring, 8, 8, 1.0F, 0.0F, 0.0F);
        CHECK(hole[4 * 8 + 4] == 0 && hole[1 * 8 + 1] == 255);

        CHECK(!Load(std::span<const std::uint8_t>{}, "x"));

        // Keyword case, a broken quote, a map without its font name.
        const auto odd = ParseFontConfig("FontLib \"Interface\\a.swf\"\nfontlib \"broken\nmap \"$Only\"\n");
        CHECK(odd.libs.size() == 1 && odd.libs[0] == "Interface\\a.swf" && odd.maps.empty());

        // Atlas level: smallest raster height still >= the line height.
        constexpr int kLevels[] = { 96, 48, 24 };
        CHECK(PickLevel(kLevels, 100.0F) == 0);
        CHECK(PickLevel(kLevels, 96.0F) == 0);
        CHECK(PickLevel(kLevels, 49.0F) == 0);
        CHECK(PickLevel(kLevels, 48.0F) == 1);
        CHECK(PickLevel(kLevels, 30.0F) == 1);
        CHECK(PickLevel(kLevels, 24.0F) == 2);
        CHECK(PickLevel(kLevels, 10.0F) == 2);

        // Optional: a real font library, e.g. NT_TEST_SWF=...\Fonticon.swf
        // NT_TEST_FONT=Iconographia; writes the "w" glyph as glyph_w.pgm.
        char swfPath[512]{};
        char fontName[128]{};
        if (GetEnvironmentVariableA("NT_TEST_SWF", swfPath, sizeof(swfPath)) && GetEnvironmentVariableA("NT_TEST_FONT", fontName, sizeof(fontName))) {
            std::ifstream                   file(swfPath, std::ios::binary);
            const std::vector<std::uint8_t> data{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            const auto                      font = Load(data, fontName);
            CHECK(font.has_value());
            if (font) {
                std::cout << std::format("[swf] {}: {} glyphs, ascent {:.3f}, descent {:.3f}\n", font->name, font->glyphs.size(), font->ascent,
                    font->descent);
                for (const auto& glyph : font->glyphs) {
                    if (glyph.code != U'w') {
                        continue;
                    }
                    constexpr int kHeight = 64;
                    const float   scale = kHeight / (font->ascent + font->descent);
                    const int     width = std::max(1, static_cast<int>(std::ceil(glyph.advance * scale)));
                    const auto    bitmap = Rasterize(glyph.contours, width, kHeight, scale, 0.0F, font->ascent * scale);
                    std::ofstream pgm("glyph_w.pgm", std::ios::binary);
                    pgm << "P5\n" << width << " " << kHeight << "\n255\n";
                    pgm.write(reinterpret_cast<const char*>(bitmap.data()), static_cast<std::streamsize>(bitmap.size()));
                    std::cout << std::format("[swf] glyph w: advance {:.3f}, {} contours, written {}x{}\n", glyph.advance, glyph.contours.size(),
                        width, kHeight);
                }
            }
        }
    }
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    TestText();
    TestMarkup();
    TestMerge();
    TestSwfFont();
    std::cout << std::format("[tests] {} passed, {} failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
