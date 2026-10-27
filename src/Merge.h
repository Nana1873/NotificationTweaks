#pragma once

// Rules that decide whether a notification continues the previous one.
// Both texts are the raw bytes the sender passed (before any re-encoding),
// because the YPS rule recomputes YPS's own byte-based split position.
namespace NT::Merge
{
    enum class Rule
    {
        kNone,
        kYps,
        kBos,
        kGeneric
    };

    // YPS Immersive Fashion (ypsThoughts.psc, playthoughtchoice): a thought
    // longer than LongThoughtLimit is sent as Substring(0, p) and
    // Substring(p + 1), where p is the first space at or after
    // max(30, len / 2 - 3). True when a + " " + b splits exactly into a/b.
    bool IsYpsSplit(std::string_view a_first, std::string_view a_second);

    // Bimbos of Skyrim wraps its thoughts in <font color="#FF00FF">. A second
    // line in the same wrapping color continues the first when the first
    // does not end a sentence.
    bool IsBosContinuation(std::string_view a_first, std::string_view a_second);

    // Any sender: the first line does not end a sentence and the second
    // starts lowercase.
    bool IsGenericContinuation(std::string_view a_first, std::string_view a_second);

    // The merged raw text: a + " " + b, keeping a single <font> wrapper when
    // both halves share one.
    std::string Join(std::string_view a_first, std::string_view a_second);
}
