#include "Settings.h"

namespace
{
    // Relative to the game CWD; resolved to an absolute path because the
    // Win32 profile API looks in the Windows directory for a bare file name.
    constexpr auto kIniRelative = "Data/SKSE/Plugins/NotificationTweaks.ini";

    NT::Settings g_settings;

    std::mutex          g_captureLock;
    NT::CaptureSettings g_capture;

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
        c.visibleTime = s.fadeIn + s.duration;
        c.fadeIn = s.fadeIn;
        c.ypsLimit = s.ypsLimit;
        std::scoped_lock lock{ g_captureLock };
        g_capture = c;
    }

    std::string IniPath()
    {
        std::error_code ec;
        const auto      path = std::filesystem::absolute(kIniRelative, ec);
        return ec ? std::string(kIniRelative) : path.string();
    }

    std::string ReadString(const char* a_section, const char* a_key, const std::string& a_default, const std::string& a_path)
    {
        char buf[512]{};
        GetPrivateProfileStringA(a_section, a_key, a_default.c_str(), buf, static_cast<DWORD>(std::size(buf)), a_path.c_str());
        return buf;
    }

    float ReadFloat(const char* a_section, const char* a_key, float a_default, float a_min, float a_max, const std::string& a_path)
    {
        const auto text = ReadString(a_section, a_key, "", a_path);
        float      value = a_default;
        if (!text.empty()) {
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec != std::errc{} || !std::isfinite(value)) {
                value = a_default;
            }
        }
        return std::clamp(value, a_min, a_max);
    }

    int ReadInt(const char* a_section, const char* a_key, int a_default, int a_min, int a_max, const std::string& a_path)
    {
        return std::clamp(static_cast<int>(GetPrivateProfileIntA(a_section, a_key, a_default, a_path.c_str())), a_min, a_max);
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
        if (text.size() != 8 || std::from_chars(text.data(), text.data() + 8, rgba, 16).ec != std::errc{}) {
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

        s.posX = ReadFloat("Layout", "PosX", d.posX, 0.0F, 1.0F, path);
        s.posY = ReadFloat("Layout", "PosY", d.posY, 0.0F, 1.0F, path);
        s.align = static_cast<Align>(ReadInt("Layout", "Align", static_cast<int>(d.align), 0, 2, path));
        s.newestOnTop = ReadBool("Layout", "NewestOnTop", d.newestOnTop, path);
        s.maxWidth = ReadFloat("Layout", "MaxWidth", d.maxWidth, 0.1F, 1.0F, path);
        s.maxVisible = ReadInt("Layout", "MaxVisible", d.maxVisible, 1, 20, path);
        s.showInMenus = ReadBool("Layout", "ShowInMenus", d.showInMenus, path);

        s.font = ReadString("Style", "Font", d.font, path);
        // "...1080" keys: older test builds stored absolute pixels as FontSize.
        s.fontSize = ReadFloat("Style", "FontSize1080", d.fontSize, 8.0F, 72.0F, path);
        s.lineSpacing = ReadFloat("Style", "LineSpacing1080", d.lineSpacing, 0.0F, 30.0F, path);
        ReadColor("Style", "TextColor", s.textColor, path);
        s.effect = static_cast<TextEffect>(ReadInt("Style", "Effect", static_cast<int>(d.effect), 0, 2, path));
        s.backgroundAlpha = ReadFloat("Style", "BackgroundAlpha", d.backgroundAlpha, 0.0F, 1.0F, path);

        s.immediate = ReadBool("Timing", "Immediate", d.immediate, path);
        s.queueDelay = ReadFloat("Timing", "QueueDelay", d.queueDelay, 0.1F, 5.0F, path);
        s.duration = ReadFloat("Timing", "Duration", d.duration, 0.5F, 30.0F, path);
        s.fadeIn = ReadFloat("Timing", "FadeIn", d.fadeIn, 0.0F, 3.0F, path);
        s.fadeOut = ReadFloat("Timing", "FadeOut", d.fadeOut, 0.0F, 5.0F, path);
        s.stackDuplicates = ReadBool("Timing", "StackDuplicates", d.stackDuplicates, path);

        s.mergeYps = ReadBool("Compatibility", "MergeYPS", d.mergeYps, path);
        s.mergeBos = ReadBool("Compatibility", "MergeBoS", d.mergeBos, path);
        s.mergeGeneric = ReadBool("Compatibility", "MergeGeneric", d.mergeGeneric, path);
        s.mergeWindow = ReadFloat("Compatibility", "MergeWindow", d.mergeWindow, 0.05F, 2.0F, path);
        s.ypsLimit = ReadInt("Compatibility", "YPSLimit", d.ypsLimit, 30, 1000, path);

        PublishCapture();
        logs::info("Settings loaded from {} (enabled {}, pos {:.3f}/{:.3f}, immediate {}, merge YPS {} BoS {} generic {})", path,
            s.enabled, s.posX, s.posY, s.immediate, s.mergeYps, s.mergeBos, s.mergeGeneric);
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
        WritePrivateProfileStringA("Style", "FontSize", nullptr, path.c_str());
        WritePrivateProfileStringA("Style", "LineSpacing", nullptr, path.c_str());
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
        PublishCapture();
    }

    CaptureSettings CaptureConfig()
    {
        std::scoped_lock lock{ g_captureLock };
        return g_capture;
    }
}
