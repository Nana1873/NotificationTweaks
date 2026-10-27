#include "Markup.h"

namespace
{
    bool IsValidUtf8(std::string_view a_text)
    {
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0) > 0;
    }

    std::string Lower(std::string_view a_text)
    {
        std::string out(a_text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    // Value of attribute a_name inside a tag body ("font color='#FF00FF' size='20'").
    std::optional<std::string> Attribute(std::string_view a_tag, std::string_view a_name)
    {
        const auto lower = Lower(a_tag);
        auto       pos = lower.find(a_name);
        while (pos != std::string::npos) {
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
                code < 0x110000) {
                wchar_t wide[2]{};
                int     count = 1;
                if (code >= 0x10000) {
                    code -= 0x10000;
                    wide[0] = static_cast<wchar_t>(0xD800 + (code >> 10));
                    wide[1] = static_cast<wchar_t>(0xDC00 + (code & 0x3FF));
                    count = 2;
                } else {
                    wide[0] = static_cast<wchar_t>(code);
                }
                char utf8[8]{};
                const auto len = WideCharToMultiByte(CP_UTF8, 0, wide, count, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
                a_out.append(utf8, static_cast<std::size_t>(std::max(len, 0)));
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
        Run                                        current;

        const auto flush = [&]() {
            if (current.text.empty()) {
                current.rgb = colors.back();  // keep a pending lineBreak
                return;
            }
            runs.push_back(std::move(current));
            current = Run{};
            current.rgb = colors.back();
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
                const auto             lower = Lower(tag);
                if (lower.starts_with("font")) {
                    const auto color = Attribute(tag, "color");
                    const auto rgb = color ? ParseColor(*color) : std::nullopt;
                    flush();
                    fontPushedColor.push_back(rgb.has_value());
                    if (rgb) {
                        colors.push_back(rgb);
                    }
                    current.rgb = colors.back();
                } else if (lower.starts_with("/font")) {
                    flush();
                    if (!fontPushedColor.empty()) {
                        if (fontPushedColor.back() && colors.size() > 1) {
                            colors.pop_back();
                        }
                        fontPushedColor.pop_back();
                    }
                    current.rgb = colors.back();
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
            out += run.text;
        }
        return out;
    }

    std::optional<std::uint32_t> WrappingColor(std::string_view a_raw)
    {
        auto trimmed = a_raw;
        while (!trimmed.empty() && trimmed.front() == ' ') {
            trimmed.remove_prefix(1);
        }
        while (!trimmed.empty() && trimmed.back() == ' ') {
            trimmed.remove_suffix(1);
        }
        const auto lower = Lower(trimmed);
        if (!lower.starts_with("<font") || !lower.ends_with("</font>")) {
            return std::nullopt;
        }
        const auto end = trimmed.find('>');
        const auto color = Attribute(trimmed.substr(1, end - 1), "color");
        return color ? ParseColor(*color) : std::nullopt;
    }
}
