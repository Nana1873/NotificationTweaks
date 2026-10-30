#include "Settings.h"

#include "Text.h"

namespace
{
    // Relative to the game CWD; resolved to an absolute path because the
    // Win32 profile API looks in the Windows directory for a bare file name.
    constexpr auto kIniRelative = "Data/SKSE/Plugins/NotificationTweaks.ini";

    NT::Settings g_settings;

    std::mutex          g_captureLock;
    NT::CaptureSettings g_capture;
    std::atomic<bool>   g_captureEnabled{ true };

    void PublishCapture()
    {
        const auto&         s = g_settings;
        NT::CaptureSettings c;
        c.enabled = s.enabled;
        c.stackDuplicates = s.stackDuplicates;
        c.mergeYps = s.mergeYps;
        c.mergeBos = s.mergeBos;
        c.mergeGeneric = s.mergeGeneric;
        c.mergeWindow = s.mergeWindow;
        c.duration = s.duration;
        c.fadeIn = s.fadeIn;
        c.ypsLimit = s.ypsLimit;
        c.drawIconFonts = s.drawIconFonts;
        std::scoped_lock lock{ g_captureLock };
        g_capture = c;
        g_captureEnabled = c.enabled;
    }

    std::string IniPath()
    {
        std::error_code ec;
        const auto      path = std::filesystem::absolute(kIniRelative, ec);
        return ec ? std::string(kIniRelative) : path.string();
    }

    // ReadString's buffer; [Hidden] and [Durations] keys must fit into it.
    constexpr std::size_t kReadBuffer = 2048;
    constexpr std::size_t kMaxKeyValue = kReadBuffer - 48;  // headroom for the quotes

    std::string ReadString(const char* a_section, const char* a_key, const std::string& a_default, const std::string& a_path)
    {
        char buf[kReadBuffer]{};
        GetPrivateProfileStringA(a_section, a_key, a_default.c_str(), buf, static_cast<DWORD>(std::size(buf)), a_path.c_str());
        return buf;
    }

    // A key list section ([Hidden], [Durations]): Count, then LineN with the
    // key and, when given, TimeN with its value.
    struct KeyLine
    {
        std::string                key;
        std::optional<float>       time;  // seconds, written with one decimal
    };

    void WriteKeySection(const char* a_section, const std::vector<KeyLine>& a_lines)
    {
        const auto path = IniPath();
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

        // One section write (one file rewrite) instead of one per key; it
        // also replaces the old section as a whole, so Count and the lines
        // never get out of step.
        std::string lines;
        int         count = 0;
        for (const auto& line : a_lines) {
            const auto value = NT::Text::PercentEscape(line.key);
            if (value.size() > kMaxKeyValue) {
                logs::warn("[{}] line too long to save ({} bytes), kept for this session only", a_section, value.size());
                continue;
            }
            // Quoted: the profile API strips outer quotes on read, so leading
            // or trailing spaces and quotes inside the text survive.
            lines += std::format("Line{}=\"{}\"", ++count, value);
            lines += '\0';
            if (line.time) {
                lines += std::format("Time{}={:.1f}", count, *line.time);
                lines += '\0';
            }
        }
        auto section = std::format("Count={}", count);
        section += '\0';
        section += lines;
        section += '\0';
        WritePrivateProfileSectionA(a_section, section.c_str(), path.c_str());
    }

    // The whole text as a finite float, or nothing.
    std::optional<float> ParseFloat(std::string_view a_text)
    {
        float      value = 0.0F;
        const auto [ptr, ec] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
        if (a_text.empty() || ec != std::errc{} || ptr != a_text.data() + a_text.size() || !std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    }

    float ReadFloat(const char* a_section, const char* a_key, float a_default, NT::Range<float> a_range, const std::string& a_path)
    {
        return std::clamp(ParseFloat(ReadString(a_section, a_key, "", a_path)).value_or(a_default), a_range.min, a_range.max);
    }

    int ReadInt(const char* a_section, const char* a_key, int a_default, NT::Range<int> a_range, const std::string& a_path)
    {
        return std::clamp(static_cast<int>(GetPrivateProfileIntA(a_section, a_key, a_default, a_path.c_str())), a_range.min, a_range.max);
    }

    // Reads a key list section written by WriteKeySection: a_line(index, key)
    // for each non-empty LineN.
    void ReadKeySection(const char* a_section, const std::string& a_path, const std::function<void(int, std::string)>& a_line)
    {
        const int count = ReadInt(a_section, "Count", 0, { 0, 10000 }, a_path);
        for (int i = 1; i <= count; ++i) {
            if (auto key = NT::Text::PercentUnescape(ReadString(a_section, std::format("Line{}", i).c_str(), "", a_path)); !key.empty()) {
                a_line(i, std::move(key));
            }
        }
    }

    bool ReadBool(const char* a_section, const char* a_key, bool a_default, const std::string& a_path)
    {
        return GetPrivateProfileIntA(a_section, a_key, a_default ? 1 : 0, a_path.c_str()) != 0;
    }

    // RRGGBBAA, the order the menu's color picker shows.
    void ReadColor(const char* a_section, const char* a_key, float (&a_color)[4], const std::string& a_path)
    {
        const auto text = ReadString(a_section, a_key, "", a_path);
        std::uint32_t rgba = 0;
        if (text.size() != 8) {
            return;
        }
        if (const auto [ptr, ec] = std::from_chars(text.data(), text.data() + 8, rgba, 16); ec != std::errc{} || ptr != text.data() + 8) {
            return;
        }
        for (int i = 0; i < 4; ++i) {
            a_color[i] = static_cast<float>((rgba >> (24 - 8 * i)) & 0xFF) / 255.0F;
        }
    }

    void Write(const char* a_section, const char* a_key, const std::string& a_value, const std::string& a_path)
    {
        WritePrivateProfileStringA(a_section, a_key, a_value.c_str(), a_path.c_str());
    }

}

namespace NT
{
    Settings& Config()
    {
        return g_settings;
    }

    void LoadSettings()
    {
        const auto path = IniPath();
        Settings   d;
        auto&      s = g_settings;

        s.enabled = ReadBool("General", "Enabled", d.enabled, path);
        s.verboseLog = ReadBool("General", "VerboseLog", d.verboseLog, path);

        s.posX = ReadFloat("Layout", "PosX", d.posX, Limits::kPos, path);
        s.posY = ReadFloat("Layout", "PosY", d.posY, Limits::kPos, path);
        s.align = static_cast<Align>(ReadInt("Layout", "Align", static_cast<int>(d.align), { 0, 2 }, path));
        s.newestOnTop = ReadBool("Layout", "NewestOnTop", d.newestOnTop, path);
        s.maxWidth = ReadFloat("Layout", "MaxWidth", d.maxWidth, Limits::kMaxWidth, path);
        s.maxVisible = ReadInt("Layout", "MaxVisible", d.maxVisible, Limits::kMaxVisible, path);
        s.showInMenus = ReadBool("Layout", "ShowInMenus", d.showInMenus, path);

        s.font = ReadString("Style", "Font", d.font, path);
        s.fontSize = ReadFloat("Style", "FontSize1080", d.fontSize, Limits::kFontSize, path);
        s.lineSpacing = ReadFloat("Style", "LineSpacing1080", d.lineSpacing, Limits::kLineSpacing, path);
        ReadColor("Style", "TextColor", s.textColor, path);
        s.effect = static_cast<TextEffect>(ReadInt("Style", "Effect", static_cast<int>(d.effect), { 0, 2 }, path));
        s.backgroundAlpha = ReadFloat("Style", "BackgroundAlpha", d.backgroundAlpha, Limits::kBackground, path);

        s.immediate = ReadBool("Timing", "Immediate", d.immediate, path);
        s.queueDelay = ReadFloat("Timing", "QueueDelay", d.queueDelay, Limits::kQueueDelay, path);
        s.duration = ReadFloat("Timing", "Duration", d.duration, Limits::kDuration, path);
        s.fadeIn = ReadFloat("Timing", "FadeIn", d.fadeIn, Limits::kFadeIn, path);
        s.fadeOut = ReadFloat("Timing", "FadeOut", d.fadeOut, Limits::kFadeOut, path);
        s.stackDuplicates = ReadBool("Timing", "StackDuplicates", d.stackDuplicates, path);

        s.mergeYps = ReadBool("Compatibility", "MergeYPS", d.mergeYps, path);
        s.mergeBos = ReadBool("Compatibility", "MergeBoS", d.mergeBos, path);
        s.mergeGeneric = ReadBool("Compatibility", "MergeGeneric", d.mergeGeneric, path);
        s.mergeWindow = ReadFloat("Compatibility", "MergeWindow", d.mergeWindow, Limits::kMergeWindow, path);
        s.ypsLimit = ReadInt("Compatibility", "YPSLimit", d.ypsLimit, Limits::kYpsLimit, path);
        s.drawIconFonts = ReadBool("Compatibility", "DrawIconFonts", d.drawIconFonts, path);

        PublishCapture();
        logs::info("Settings loaded from {} (enabled {}, pos {:.3f}/{:.3f}, immediate {}, merge YPS {} BoS {} generic {}, icon fonts {})",
            path, s.enabled, s.posX, s.posY, s.immediate, s.mergeYps, s.mergeBos, s.mergeGeneric, s.drawIconFonts);
    }

    void SaveSettings()
    {
        const auto  path = IniPath();
        const auto& s = g_settings;
        const auto  f = [](float a_value) { return std::format("{:.4f}", a_value); };
        const auto  b = [](bool a_value) { return std::string(a_value ? "1" : "0"); };

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

        Write("General", "Enabled", b(s.enabled), path);
        Write("General", "VerboseLog", b(s.verboseLog), path);

        Write("Layout", "PosX", f(s.posX), path);
        Write("Layout", "PosY", f(s.posY), path);
        Write("Layout", "Align", std::to_string(static_cast<int>(s.align)), path);
        Write("Layout", "NewestOnTop", b(s.newestOnTop), path);
        Write("Layout", "MaxWidth", f(s.maxWidth), path);
        Write("Layout", "MaxVisible", std::to_string(s.maxVisible), path);
        Write("Layout", "ShowInMenus", b(s.showInMenus), path);

        std::uint32_t rgba = 0;
        for (int i = 0; i < 4; ++i) {
            rgba |= static_cast<std::uint32_t>(std::lround(std::clamp(s.textColor[i], 0.0F, 1.0F) * 255.0F)) << (24 - 8 * i);
        }
        Write("Style", "Font", s.font, path);
        Write("Style", "FontSize1080", f(s.fontSize), path);
        Write("Style", "LineSpacing1080", f(s.lineSpacing), path);
        Write("Style", "TextColor", std::format("{:08X}", rgba), path);
        Write("Style", "Effect", std::to_string(static_cast<int>(s.effect)), path);
        Write("Style", "BackgroundAlpha", f(s.backgroundAlpha), path);

        Write("Timing", "Immediate", b(s.immediate), path);
        Write("Timing", "QueueDelay", f(s.queueDelay), path);
        Write("Timing", "Duration", f(s.duration), path);
        Write("Timing", "FadeIn", f(s.fadeIn), path);
        Write("Timing", "FadeOut", f(s.fadeOut), path);
        Write("Timing", "StackDuplicates", b(s.stackDuplicates), path);

        Write("Compatibility", "MergeYPS", b(s.mergeYps), path);
        Write("Compatibility", "MergeBoS", b(s.mergeBos), path);
        Write("Compatibility", "MergeGeneric", b(s.mergeGeneric), path);
        Write("Compatibility", "MergeWindow", f(s.mergeWindow), path);
        Write("Compatibility", "YPSLimit", std::to_string(s.ypsLimit), path);
        Write("Compatibility", "DrawIconFonts", b(s.drawIconFonts), path);
        PublishCapture();
    }

    void ResetDisplaySettings()
    {
        auto&          s = g_settings;
        const Settings keep = s;
        s = Settings{};
        s.enabled = keep.enabled;
        s.verboseLog = keep.verboseLog;
        s.mergeYps = keep.mergeYps;
        s.mergeBos = keep.mergeBos;
        s.mergeGeneric = keep.mergeGeneric;
        s.mergeWindow = keep.mergeWindow;
        s.ypsLimit = keep.ypsLimit;
        s.drawIconFonts = keep.drawIconFonts;
        SaveSettings();
    }

    CaptureSettings CaptureConfig()
    {
        std::scoped_lock lock{ g_captureLock };
        return g_capture;
    }

    bool CaptureEnabled()
    {
        return g_captureEnabled;
    }

    std::vector<std::string> ReadHiddenKeys()
    {
        std::vector<std::string> keys;
        ReadKeySection("Hidden", IniPath(), [&](int, std::string a_key) { keys.push_back(std::move(a_key)); });
        logs::info("{} hidden line(s) loaded", keys.size());
        return keys;
    }

    void WriteHiddenKeys(const std::vector<std::string>& a_keys)
    {
        std::vector<KeyLine> lines;
        for (const auto& key : a_keys) {
            lines.push_back({ key, std::nullopt });
        }
        WriteKeySection("Hidden", lines);
    }

    std::map<std::string, float> ReadDurations()
    {
        const auto                   path = IniPath();
        std::map<std::string, float> durations;
        ReadKeySection("Durations", path, [&](int a_index, std::string a_key) {
            // A line without a valid time is dropped, not given a default one.
            if (const auto seconds = ParseFloat(ReadString("Durations", std::format("Time{}", a_index).c_str(), "", path))) {
                durations[std::move(a_key)] = std::clamp(*seconds, Limits::kDuration.min, Limits::kDuration.max);
            }
        });
        logs::info("{} own display time(s) loaded", durations.size());
        return durations;
    }

    void WriteDurations(const std::map<std::string, float>& a_durations)
    {
        std::vector<KeyLine> lines;
        for (const auto& [key, seconds] : a_durations) {
            lines.push_back({ key, seconds });
        }
        WriteKeySection("Durations", lines);
    }
}
