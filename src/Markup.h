#pragma once

// The small HTML subset the Vanilla notification text field renders
// (Scaleform htmlText): <font color="#RRGGBB">, <br>, entities. Everything
// else is dropped as a tag.
namespace NT::Markup
{
    struct Run
    {
        std::string                  text;  // UTF-8
        std::optional<std::uint32_t> rgb;   // 0xRRGGBB from <font color>, nullopt = default color
        bool                         lineBreak = false;  // <br> / '\n' before this run
    };

    // Notification text arrives in whatever encoding the sender used:
    // UTF-8 from most sources, Windows-1252 from Papyrus strings read out of
    // legacy files (YPS thought JSON). Invalid UTF-8 is treated as 1252.
    std::string ToUtf8(std::string_view a_raw);

    std::vector<Run> Parse(std::string_view a_raw);

    // Tag-free text (entities decoded), for the merge heuristics.
    std::string Visible(std::string_view a_raw);

    // The color of the outermost <font color> when it wraps the whole text.
    std::optional<std::uint32_t> WrappingColor(std::string_view a_raw);
}
