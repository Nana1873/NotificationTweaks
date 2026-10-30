#pragma once

// Small text helpers shared by the modules (and the unit tests): UTF-8 <->
// UTF-16 for the Win32 Unicode APIs, case mapping, trimming, code points,
// hide keys and the %XX escaping of INI values. Header-only and free of
// game types.
namespace NT::Text
{
    inline std::wstring ToWide(std::string_view a_utf8)
    {
        if (a_utf8.empty()) {
            return {};
        }
        const auto size = MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), result.data(), size);
        return result;
    }

    inline std::string ToUtf8(std::wstring_view a_wide)
    {
        if (a_wide.empty()) {
            return {};
        }
        const auto size =
            WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, a_wide.data(), static_cast<int>(a_wide.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    // ASCII-only lowercase, for tag, attribute and font names.
    inline std::string LowerAscii(std::string_view a_text)
    {
        std::string out(a_text);
        for (auto& c : out) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return out;
    }

    // Unicode lowercase (CharLowerBuffW), UTF-8 in and out.
    inline std::string Lower(std::string_view a_utf8)
    {
        auto wide = ToWide(a_utf8);
        if (!wide.empty()) {
            ::CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
        }
        return ToUtf8(wide);
    }

    // Without leading and trailing spaces and tabs.
    inline std::string_view Trim(std::string_view a_text)
    {
        while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) {
            a_text.remove_prefix(1);
        }
        while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t')) {
            a_text.remove_suffix(1);
        }
        return a_text;
    }

    // Appends code point a_code (below 0x110000, no surrogate) as UTF-8.
    inline void AppendUtf8(std::uint32_t a_code, std::string& a_out)
    {
        if (a_code < 0x80) {
            a_out += static_cast<char>(a_code);
        } else if (a_code < 0x800) {
            a_out += static_cast<char>(0xC0 | (a_code >> 6));
            a_out += static_cast<char>(0x80 | (a_code & 0x3F));
        } else if (a_code < 0x10000) {
            a_out += static_cast<char>(0xE0 | (a_code >> 12));
            a_out += static_cast<char>(0x80 | ((a_code >> 6) & 0x3F));
            a_out += static_cast<char>(0x80 | (a_code & 0x3F));
        } else {
            a_out += static_cast<char>(0xF0 | (a_code >> 18));
            a_out += static_cast<char>(0x80 | ((a_code >> 12) & 0x3F));
            a_out += static_cast<char>(0x80 | ((a_code >> 6) & 0x3F));
            a_out += static_cast<char>(0x80 | (a_code & 0x3F));
        }
    }

    // The code point at a_pos (advanced past it); U+FFFD for a broken
    // sequence. Requires a_pos < a_text.size().
    inline char32_t NextCodePoint(std::string_view a_text, std::size_t& a_pos)
    {
        const auto byte = static_cast<unsigned char>(a_text[a_pos++]);
        const int  extra = byte < 0x80 ? 0 : (byte >> 5) == 0x06 ? 1 : (byte >> 4) == 0x0E ? 2 : (byte >> 3) == 0x1E ? 3 : -1;
        if (extra < 0) {
            return U'\uFFFD';
        }
        char32_t code = extra == 0 ? byte : byte & (0x3F >> extra);
        for (int i = 0; i < extra; ++i) {
            if (a_pos >= a_text.size() || (static_cast<unsigned char>(a_text[a_pos]) & 0xC0) != 0x80) {
                return U'\uFFFD';
            }
            code = (code << 6) | (static_cast<unsigned char>(a_text[a_pos++]) & 0x3F);
        }
        return code;
    }

    // The hide key of a visible line: every run of ASCII digits becomes "#",
    // so "25 Gold added" and "30 Gold added" share one key. ASCII only:
    // std::isdigit depends on the CRT locale and can match UTF-8
    // continuation bytes (0xB2, 0xB3, 0xB9 in cp1252).
    inline std::string HideKey(std::string_view a_visible)
    {
        const auto digit = [](char c) { return c >= '0' && c <= '9'; };
        std::string key;
        key.reserve(a_visible.size());
        for (std::size_t i = 0; i < a_visible.size(); ++i) {
            if (digit(a_visible[i])) {
                key += '#';
                while (i + 1 < a_visible.size() && digit(a_visible[i + 1])) {
                    ++i;
                }
            } else {
                key += a_visible[i];
            }
        }
        return key;
    }

    // "%XX" for bytes >= 0x80, < 0x20 and '%': keeps a value ASCII for the
    // profile A-API, which converts through the ANSI codepage and breaks
    // UTF-8 on DBCS locales.
    inline std::string PercentEscape(std::string_view a_text)
    {
        std::string out;
        for (const char c : a_text) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte >= 0x80 || byte < 0x20 || c == '%') {
                out += std::format("%{:02X}", byte);
            } else {
                out += c;
            }
        }
        return out;
    }

    // Doubles every '%', so a translated text can be an ImGui format string
    // that shows as plain text.
    inline std::string EscapeFormat(std::string_view a_text)
    {
        std::string out;
        for (const char c : a_text) {
            out += c;
            if (c == '%') {
                out += '%';
            }
        }
        return out;
    }

    // Reverses PercentEscape; a '%' without two hex digits stays as it is.
    inline std::string PercentUnescape(std::string_view a_text)
    {
        std::string out;
        for (std::size_t i = 0; i < a_text.size(); ++i) {
            std::uint8_t byte = 0;
            if (a_text[i] == '%' && i + 3 <= a_text.size() &&
                std::from_chars(a_text.data() + i + 1, a_text.data() + i + 3, byte, 16).ptr == a_text.data() + i + 3) {
                out += static_cast<char>(byte);
                i += 2;
            } else {
                out += a_text[i];
            }
        }
        return out;
    }
}
