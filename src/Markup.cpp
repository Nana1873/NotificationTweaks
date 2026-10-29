#include "Markup.h"

#include "Text.h"

namespace
{
    bool IsValidUtf8(std::string_view a_text)
    {
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0) > 0;
    }

    // Value of attribute a_name inside a tag body ("font color='#FF00FF' size='20'").
    std::optional<std::string> Attribute(std::string_view a_tag, std::string_view a_name)
    {
        const auto lower = NT::Text::LowerAscii(a_tag);
        auto       pos = lower.find(a_name);
        while (pos != std::string::npos) {
            // Whole attribute names only ("color", not the tail of "bgcolor").
            if (pos > 0 && !std::isspace(static_cast<unsigned char>(lower[pos - 1]))) {
                pos = lower.find(a_name, pos + 1);
                continue;
            }
            auto i = pos + a_name.size();
            while (i < lower.size() && lower[i] == ' ') {
                ++i;
            }
            if (i < lower.size() && lower[i] == '=') {
                ++i;
                while (i < lower.size() && lower[i] == ' ') {
                    ++i;
                }
                char quote = 0;
                if (i < lower.size() && (lower[i] == '"' || lower[i] == '\'')) {
                    quote = lower[i++];
                }
                const auto start = i;
                while (i < lower.size() && (quote ? lower[i] != quote : (lower[i] != ' ' && lower[i] != '>'))) {
                    ++i;
                }
                return std::string(a_tag.substr(start, i - start));
            }
            pos = lower.find(a_name, pos + 1);
        }
        return std::nullopt;
    }

    std::optional<std::uint32_t> ParseColor(std::string_view a_value)
    {
        if (!a_value.empty() && a_value.front() == '#') {
            a_value.remove_prefix(1);
        } else if (a_value.size() > 2 && (a_value.starts_with("0x") || a_value.starts_with("0X"))) {
            a_value.remove_prefix(2);
        }
        std::uint32_t rgb = 0;
        if (a_value.size() != 6 || std::from_chars(a_value.data(), a_value.data() + 6, rgb, 16).ec != std::errc{}) {
            return std::nullopt;
        }
        return rgb;
    }

    void AppendEntity(std::string_view a_entity, std::string& a_out)
    {
        static const std::pair<std::string_view, std::string_view> kNamed[] = {
            { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" }, { "nbsp", " " }
        };
        for (const auto& [name, value] : kNamed) {
            if (a_entity == name) {
                a_out += value;
                return;
            }
        }
        if (a_entity.size() > 1 && a_entity.front() == '#') {
            std::uint32_t code = 0;
            const bool    hex = a_entity.size() > 2 && (a_entity[1] == 'x' || a_entity[1] == 'X');
            const auto    digits = a_entity.substr(hex ? 2 : 1);
            if (std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10).ec == std::errc{} && code > 0 &&
                code < 0x110000 && (code < 0xD800 || code >= 0xE000)) {
                NT::Text::AppendUtf8(code, a_out);
                return;
            }
        }
        a_out += '&';
        a_out += a_entity;
        a_out += ';';
    }
}

namespace NT::Markup
{
    bool IsIconFace(std::string_view a_face)
    {
        const auto lower = NT::Text::LowerAscii(a_face);
        return lower.find("icon") != std::string::npos || lower.find("symbol") != std::string::npos;
    }

    std::string ToUtf8(std::string_view a_raw)
    {
        if (a_raw.empty() || IsValidUtf8(a_raw)) {
            return std::string(a_raw);
        }
        const auto   wideLen = MultiByteToWideChar(1252, 0, a_raw.data(), static_cast<int>(a_raw.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
        MultiByteToWideChar(1252, 0, a_raw.data(), static_cast<int>(a_raw.size()), wide.data(), wideLen);
        const auto  len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, out.data(), len, nullptr, nullptr);
        return out;
    }

    std::vector<Run> Parse(std::string_view a_raw)
    {
        const auto text = ToUtf8(a_raw);

        std::vector<Run>                           runs;
        std::vector<std::optional<std::uint32_t>> colors{ std::nullopt };
        std::vector<bool>                          fontPushedColor;  // per open <font>: did it push a color?
        std::vector<std::string>                   iconFaces{ {} };  // per open <font>: its icon face, empty for text
        Run                                        current;

        const auto flush = [&]() {
            if (current.text.empty()) {
                current.rgb = colors.back();  // keep a pending lineBreak
                current.iconFace = iconFaces.back();
                return;
            }
            runs.push_back(std::move(current));
            current = Run{};
            current.rgb = colors.back();
            current.iconFace = iconFaces.back();
        };
        current.rgb = colors.back();

        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '<') {
                const auto end = text.find('>', i);
                if (end == std::string::npos) {
                    current.text.append(text, i, std::string::npos);
                    break;
                }
                const std::string_view tag{ text.data() + i + 1, end - i - 1 };
                const auto             lower = NT::Text::LowerAscii(tag);
                if (lower.starts_with("font")) {
                    const auto color = Attribute(tag, "color");
                    const auto rgb = color ? ParseColor(*color) : std::nullopt;
                    const auto face = Attribute(tag, "face");
                    flush();
                    fontPushedColor.push_back(rgb.has_value());
                    if (rgb) {
                        colors.push_back(rgb);
                    }
                    iconFaces.push_back(face ? (NT::Markup::IsIconFace(*face) ? *face : std::string{}) : iconFaces.back());
                    current.rgb = colors.back();
                    current.iconFace = iconFaces.back();
                } else if (lower.starts_with("/font")) {
                    flush();
                    if (!fontPushedColor.empty()) {
                        if (fontPushedColor.back() && colors.size() > 1) {
                            colors.pop_back();
                        }
                        fontPushedColor.pop_back();
                    }
                    if (iconFaces.size() > 1) {
                        iconFaces.pop_back();
                    }
                    current.rgb = colors.back();
                    current.iconFace = iconFaces.back();
                } else if (lower.starts_with("br") || lower.starts_with("/p")) {
                    flush();
                    current.lineBreak = true;
                }
                i = end;
            } else if (c == '&') {
                const auto end = text.find(';', i);
                if (end != std::string::npos && end - i <= 10) {
                    AppendEntity(std::string_view{ text.data() + i + 1, end - i - 1 }, current.text);
                    i = end;
                } else {
                    current.text += c;
                }
            } else if (c == '\n') {
                flush();
                current.lineBreak = true;
            } else if (c != '\r') {
                current.text += c;
            }
        }
        if (!current.text.empty()) {
            runs.push_back(std::move(current));
        }
        return runs;
    }

    std::string Visible(std::string_view a_raw)
    {
        std::string out;
        for (const auto& run : Parse(a_raw)) {
            if (run.lineBreak && !out.empty()) {
                out += ' ';
            }
            if (run.iconFace.empty()) {
                out += run.text;  // icon glyphs are letters only in their own font
            }
        }
        // A skipped icon leaves the space that followed it.
        return std::string(NT::Text::Trim(out));
    }

    IconList Icons(std::string_view a_raw)
    {
        IconList icons;
        for (const auto& run : Parse(a_raw)) {
            for (std::size_t i = 0; !run.iconFace.empty() && i < run.text.size();) {
                if (const auto code = NT::Text::NextCodePoint(run.text, i); code != U' ' && code != U'\t') {
                    icons.emplace_back(run.iconFace, code);
                }
            }
        }
        return icons;
    }

    std::string IconKey(const IconList& a_icons)
    {
        if (a_icons.empty()) {
            return {};
        }
        std::string key = "[icon]";
        for (const auto& [face, code] : a_icons) {
            key += ' ';
            key += face;
            key += ':';
            NT::Text::AppendUtf8(code, key);
        }
        return key;
    }

    std::optional<std::uint32_t> WrappingColor(std::string_view a_raw)
    {
        const auto trimmed = NT::Text::Trim(a_raw);
        const auto lower = NT::Text::LowerAscii(trimmed);
        if (!lower.starts_with("<font") || !lower.ends_with("</font>")) {
            return std::nullopt;
        }
        const auto end = trimmed.find('>');
        const auto color = Attribute(trimmed.substr(1, end - 1), "color");
        return color ? ParseColor(*color) : std::nullopt;
    }
}
