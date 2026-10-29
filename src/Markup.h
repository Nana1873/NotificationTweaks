#pragma once

// The small HTML subset the Vanilla notification text field renders
// (Scaleform htmlText): <font color="#RRGGBB">, <br>, entities. Everything
// else is dropped as a tag. Text inside <font face="..."> of an icon font
// (name contains "icon" or "symbol") becomes runs with iconFace set: its
// letters stand for pictures (IconFont draws them), not for text.
namespace NT::Markup
{
    struct Run
    {
        std::string                  text;  // UTF-8
        std::optional<std::uint32_t> rgb;   // 0xRRGGBB from <font color>, nullopt = default color
        bool                         lineBreak = false;  // <br> / '\n' before this run
        std::string                  iconFace;           // "$Iconographia": text is icon-font glyphs
    };

    // Icon fonts map pictures onto letters ("$Iconographia" from Oblivion
    // Interaction Icons draws its save icon as "w", the game's own
    // "$SkyrimSymbolsFont" works the same way): a face whose name contains
    // "icon" or "symbol". Parse marks such runs with iconFace (IconFont
    // draws them); Visible() leaves them out.
    bool IsIconFace(std::string_view a_face);

    // Notification text arrives in whatever encoding the sender used:
    // UTF-8 from most sources, Windows-1252 from Papyrus strings read out of
    // legacy files (YPS thought JSON). Invalid UTF-8 is treated as 1252.
    std::string ToUtf8(std::string_view a_raw);

    std::vector<Run> Parse(std::string_view a_raw);

    // Tag-free text (entities decoded, icon glyphs left out, trimmed): merge
    // heuristics, history and hide keys.
    std::string Visible(std::string_view a_raw);

    // The icon-font glyphs of a line as (face, code), whitespace skipped.
    using IconList = std::vector<std::pair<std::string, char32_t>>;
    IconList Icons(std::string_view a_raw);

    // Key of a line's icon glyphs ("[icon] $iconographia:w", face lowercased
    // as IconFont matches it); empty without icons.
    std::string IconKey(const IconList& a_icons);

    // The hide key of a line (the eye on the History page, [Hidden] in the
    // INI): its text with digit runs as "#" (Text::HideKey), or IconKey for
    // a line without text. Empty: the line cannot be hidden.
    std::string LineHideKey(std::string_view a_visible, const IconList& a_icons);
    std::string LineHideKey(std::string_view a_raw);

    // The color of the outermost <font color> when it wraps the whole text.
    std::optional<std::uint32_t> WrappingColor(std::string_view a_raw);
}
