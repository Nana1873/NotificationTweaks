#include "Merge.h"

#include "Markup.h"
#include "Text.h"

namespace
{
    constexpr std::uint32_t kBosColor = 0xFF00FF;

    // ypsThoughts.psc never splits before this byte position.
    constexpr std::size_t kYpsMinSplit = 30;

    // YPS thought entries start with a sound tag "[nnnnn]".
    constexpr std::size_t kSoundTagLength = 7;

    // Only thoughts longer than this are stored. YPS's default limit is 80,
    // so shorter ones are never split. With a YPSLimit under 60, split
    // thoughts of 31 to 60 bytes merge only when they also pass the
    // narration check in IsYpsSplit (no markup, mid-sentence, lowercase).
    constexpr std::size_t kMinStoredThought = 60;

    // Full YPS thoughts (sound prefix "[nnnnn]" removed) long enough to be split.
    std::unordered_set<std::string> g_ypsThoughts;

    // Exactly four hex digits.
    bool Hex4(std::string_view a_digits, std::uint32_t& a_out)
    {
        if (a_digits.size() != 4) {
            return false;
        }
        const auto [ptr, ec] = std::from_chars(a_digits.data(), a_digits.data() + 4, a_out, 16);
        return ec == std::errc{} && ptr == a_digits.data() + 4;
    }

    // Every JSON string literal in a_json (keys included; they are short and
    // harmless). Bytes outside escapes are kept as they are, like JsonUtil does.
    void CollectStrings(std::string_view a_json, std::unordered_set<std::string>& a_out)
    {
        for (std::size_t i = 0; i < a_json.size(); ++i) {
            if (a_json[i] != '"') {
                continue;
            }
            std::string value;
            for (++i; i < a_json.size() && a_json[i] != '"'; ++i) {
                if (a_json[i] != '\\' || i + 1 >= a_json.size()) {
                    value += a_json[i];
                    continue;
                }
                const char escape = a_json[++i];
                switch (escape) {
                case 'n':
                    value += '\n';
                    break;
                case 't':
                    value += '\t';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case 'b':
                case 'f':
                    break;
                case 'u':
                    {
                        std::uint32_t code = 0;
                        if (i + 4 < a_json.size() && Hex4(a_json.substr(i + 1, 4), code)) {
                            i += 4;
                            std::uint32_t low = 0;
                            if (code >= 0xD800 && code < 0xDC00 && i + 6 < a_json.size() && a_json[i + 1] == '\\' && a_json[i + 2] == 'u' &&
                                Hex4(a_json.substr(i + 3, 4), low) && low >= 0xDC00 && low < 0xE000) {
                                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                                i += 6;
                            }
                            if (code < 0xD800 || code >= 0xE000) {
                                NT::Text::AppendUtf8(code, value);  // a lone surrogate is dropped
                            }
                        }
                        break;
                    }
                default:
                    value += escape;  // \" \\ \/
                    break;
                }
            }
            if (value.size() > kSoundTagLength && value[0] == '[' && value[kSoundTagLength - 1] == ']') {
                value.erase(0, kSoundTagLength);
            }
            if (value.size() > kMinStoredThought) {
                a_out.insert(std::move(value));
            }
        }
    }

    // '.', '!', '?', ':' and the UTF-8 ellipsis end a sentence; a closing
    // quote or bracket after them still counts.
    bool EndsSentence(std::string_view a_visible)
    {
        auto text = NT::Text::Trim(a_visible);
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
        const auto text = NT::Text::Trim(a_visible);
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
    std::size_t YpsSplitPos(std::string_view a_text)
    {
        auto split = std::max<std::size_t>(kYpsMinSplit, a_text.size() / 2 > 3 ? a_text.size() / 2 - 3 : 0);
        while (split < a_text.size() && a_text[split] != ' ') {
            ++split;
        }
        return std::min(split, a_text.size());
    }

    bool IsYpsSplit(std::string_view a_first, std::string_view a_second, int a_limit)
    {
        if (a_first.size() < kYpsMinSplit || a_second.empty()) {
            return false;
        }
        const std::string full = std::string(a_first) + " " + std::string(a_second);
        const auto        length = static_cast<std::ptrdiff_t>(full.size());
        if (length <= a_limit) {
            return false;  // YPS sends thoughts up to its limit unsplit
        }
        if (YpsSplitPos(full) != a_first.size()) {
            return false;
        }
        if (g_ypsThoughts.contains(full)) {
            return true;
        }
        // YpsPiercingTicker.shownotification splits its ~500 narration lines
        // (with actor names, so in no database) the same way. Those carry no
        // markup and are cut mid-sentence. Load-time bursts of unrelated
        // lines without punctuation ("X hinzugefügt", "Y Loaded") fit the
        // formula too, so the second half must also start lowercase.
        return full.find('<') == std::string::npos && !EndsSentence(a_first) && StartsLowercase(a_second);
    }

    void LoadYpsThoughts()
    {
        g_ypsThoughts.clear();
        std::error_code ec;
        const auto      dir = std::filesystem::absolute("Data/SKSE/Plugins/ypsThoughts", ec);
        std::size_t     files = 0;
        if (!ec && std::filesystem::is_directory(dir, ec)) {
            for (const auto& item : std::filesystem::directory_iterator(dir, ec)) {
                if (!item.is_regular_file() || item.path().extension() != ".json") {
                    continue;
                }
                std::ifstream file(item.path(), std::ios::binary);
                const std::string content{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
                CollectStrings(content, g_ypsThoughts);
                ++files;
            }
        }
        logs::info("YPS thoughts: {} long thought(s) from {} file(s) in {}", g_ypsThoughts.size(), files, dir.string());
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
        return std::string(NT::Text::Trim(a_first)) + " " + std::string(NT::Text::Trim(a_second));
    }
}
