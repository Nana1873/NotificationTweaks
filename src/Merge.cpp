#include "Merge.h"

#include "Markup.h"

namespace
{
    constexpr std::uint32_t kBosColor = 0xFF00FF;

    std::string_view Trim(std::string_view a_text)
    {
        while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) {
            a_text.remove_prefix(1);
        }
        while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t')) {
            a_text.remove_suffix(1);
        }
        return a_text;
    }

    // '.', '!', '?', ':' and the UTF-8 ellipsis end a sentence; a closing
    // quote or bracket after them still counts.
    bool EndsSentence(std::string_view a_visible)
    {
        auto text = Trim(a_visible);
        while (!text.empty() && (text.back() == '"' || text.back() == '\'' || text.back() == ')' || text.back() == '*')) {
            text.remove_suffix(1);
        }
        if (text.empty()) {
            return true;
        }
        if (text.ends_with("\xE2\x80\xA6")) {  // U+2026
            return true;
        }
        const char last = text.back();
        return last == '.' || last == '!' || last == '?' || last == ':';
    }

    bool StartsLowercase(std::string_view a_visible)
    {
        const auto text = Trim(a_visible);
        if (text.empty()) {
            return false;
        }
        const auto   utf8 = std::string(text.substr(0, std::min<std::size_t>(text.size(), 4)));
        wchar_t      wide[4]{};
        const auto   count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide, 4);
        if (count <= 0) {
            return false;
        }
        return IsCharLowerW(wide[0]) != FALSE;
    }

    std::string Inner(std::string_view a_raw)
    {
        const auto open = a_raw.find('>');
        const auto close = a_raw.rfind('<');
        if (open == std::string_view::npos || close == std::string_view::npos || close <= open) {
            return std::string(a_raw);
        }
        return std::string(a_raw.substr(open + 1, close - open - 1));
    }
}

namespace NT::Merge
{
    bool IsYpsSplit(std::string_view a_first, std::string_view a_second)
    {
        if (a_first.size() < 30 || a_second.empty()) {
            return false;
        }
        const std::string full = std::string(a_first) + " " + std::string(a_second);
        const auto        length = static_cast<std::ptrdiff_t>(full.size());
        auto              split = std::max<std::ptrdiff_t>(30, length / 2 - 3);
        while (split < length && full[static_cast<std::size_t>(split)] != ' ') {
            ++split;
        }
        return split == static_cast<std::ptrdiff_t>(a_first.size());
    }

    bool IsBosContinuation(std::string_view a_first, std::string_view a_second)
    {
        const auto first = Markup::WrappingColor(a_first);
        const auto second = Markup::WrappingColor(a_second);
        if (!first || !second || *first != kBosColor || *second != kBosColor) {
            return false;
        }
        return !EndsSentence(Markup::Visible(a_first));
    }

    bool IsGenericContinuation(std::string_view a_first, std::string_view a_second)
    {
        return !EndsSentence(Markup::Visible(a_first)) && StartsLowercase(Markup::Visible(a_second));
    }

    std::string Join(std::string_view a_first, std::string_view a_second)
    {
        const auto first = Markup::WrappingColor(a_first);
        const auto second = Markup::WrappingColor(a_second);
        if (first && second && *first == *second) {
            const auto open = a_first.substr(0, a_first.find('>') + 1);
            return std::string(open) + Inner(a_first) + " " + Inner(a_second) + "</font>";
        }
        return std::string(Trim(a_first)) + " " + std::string(Trim(a_second));
    }
}
