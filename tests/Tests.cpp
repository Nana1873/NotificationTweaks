// Unit tests for the pure parts of the plugin: merge rules, markup parsing,
// hide keys and INI escaping. Built with the plugin; tools\build.ps1 runs
// the exe and fails the build when a check fails.

#include "Markup.h"
#include "Merge.h"
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

        CHECK(Trim("  \tx y\t ") == "x y");
        CHECK(Trim("   ").empty());
        CHECK(Lower("\xC3\x84" "BC") == "\xC3\xA4" "bc");

        std::string utf8;
        AppendUtf8(0x1F600, utf8);
        CHECK(utf8 == "\xF0\x9F\x98\x80");
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
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    TestText();
    TestMarkup();
    TestMerge();
    std::cout << std::format("[tests] {} passed, {} failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
