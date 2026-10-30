#pragma once

// Plugin settings, persisted in Data/SKSE/Plugins/NotificationTweaks.ini
// (under MO2 the file lands in overwrite/ on first save). Read once at
// kDataLoaded, written by the menu whenever a value is committed.
namespace NT
{
    enum class Align : int
    {
        kLeft = 0,
        kCenter = 1,
        kRight = 2
    };

    enum class TextEffect : int
    {
        kNone = 0,
        kShadow = 1,
        kOutline = 2
    };

    template <class T>
    struct Range
    {
        T min;
        T max;
    };

    // Allowed values: LoadSettings clamps the INI to them and the menu's
    // sliders use the same bounds.
    namespace Limits
    {
        inline constexpr Range<float> kPos{ 0.0F, 1.0F };
        inline constexpr Range<float> kMaxWidth{ 0.1F, 1.0F };
        inline constexpr Range<int>   kMaxVisible{ 1, 20 };
        inline constexpr Range<float> kFontSize{ 8.0F, 72.0F };
        inline constexpr Range<float> kLineSpacing{ 0.0F, 30.0F };
        inline constexpr Range<float> kBackground{ 0.0F, 1.0F };
        inline constexpr Range<float> kQueueDelay{ 0.1F, 5.0F };
        inline constexpr Range<float> kDuration{ 0.5F, 30.0F };
        inline constexpr Range<float> kFadeIn{ 0.0F, 3.0F };
        inline constexpr Range<float> kFadeOut{ 0.0F, 5.0F };
        inline constexpr Range<float> kMergeWindow{ 0.05F, 2.0F };
        inline constexpr Range<int>   kYpsLimit{ 30, 1000 };
    }

    // Font size and line spacing are stored as pixels at this screen height.
    inline constexpr float kReferenceHeight = 1080.0F;

    struct Settings
    {
        // [General]
        bool enabled = true;  // false: Vanilla HUD shows the notifications again
        bool verboseLog = false;  // INI only: log every notification and merge (debug level)

        // [Layout] PosX/PosY are fractions of the screen and mark the anchor
        // of the block: its top edge, and depending on Align its left edge,
        // horizontal center or right edge.
        float posX = 0.025F;
        float posY = 0.06F;
        Align align = Align::kLeft;
        bool  newestOnTop = false;
        float maxWidth = 0.45F;  // fraction of the screen width; longer lines wrap
        int   maxVisible = 6;
        bool  showInMenus = false;  // false: lines wait while a full-screen menu (inventory, map) is open

        // [Style]
        std::string font;  // ImGui font name from the SKSE Menu Framework atlas; empty = default
        // Pixels at 1080p; the overlay scales both with the screen height so
        // the defaults look the same at every resolution.
        float       fontSize = 20.0F;
        float       lineSpacing = 3.0F;
        float       textColor[4] = { 1.0F, 1.0F, 1.0F, 1.0F };
        TextEffect  effect = TextEffect::kShadow;
        float       backgroundAlpha = 0.0F;

        // [Timing]
        bool  immediate = true;      // false: one line per QueueDelay, like the Vanilla queue
        float queueDelay = 1.0F;     // seconds between lines when not immediate
        float duration = 4.0F;       // seconds fully visible
        float fadeIn = 0.15F;
        float fadeOut = 0.75F;
        bool  stackDuplicates = true;  // identical visible text -> "(x2)" instead of a new line

        // [Compatibility]
        bool  mergeYps = true;      // YPS Immersive Fashion: thoughts split in two halves
        bool  mergeBos = true;      // Bimbos of Skyrim: continuation in the same <font> color
        bool  mergeGeneric = false; // any two lines: no sentence end + lowercase continuation
        float mergeWindow = 0.35F;  // seconds the second half may arrive after the first
        int   ypsLimit = 80;        // YPS LongThoughtLimit (MCM); only longer thoughts are split
        bool  drawIconFonts = true; // draw icon-font glyphs (Oblivion Interaction Icons) from the library fontconfig.txt maps the face to
    };

    // The values the game thread reads while capturing. The menu and the
    // overlay own Settings on the render thread; LoadSettings/SaveSettings
    // publish this copy under a lock, so the game thread never reads
    // Settings itself.
    struct CaptureSettings
    {
        bool  enabled = true;
        bool  stackDuplicates = true;
        bool  mergeYps = true;
        bool  mergeBos = true;
        bool  mergeGeneric = false;
        float mergeWindow = 0.35F;
        float duration = 4.0F;  // general display time; with fadeIn it bounds the stacking window unless a line has its own time
        float fadeIn = 0.15F;
        int   ypsLimit = 80;
        bool  drawIconFonts = true;
    };

    // Render thread (menu, overlay) and startup: the full settings.
    Settings& Config();
    void      LoadSettings();
    void      SaveSettings();

    // Menu reset: display, layout and timing back to defaults; the switch,
    // logging and the compatibility rules stay. Saves.
    void ResetDisplaySettings();

    // Any thread: a copy of the capture values as last loaded or saved.
    CaptureSettings CaptureConfig();

    // Any thread, lock-free: CaptureConfig().enabled, for the per-frame hooks.
    bool CaptureEnabled();

    // [Hidden]: the hide keys (Markup::LineHideKey) of lines the player hid in
    // the HUD. Capture owns the live set and calls these.
    std::vector<std::string> ReadHiddenKeys();
    void                     WriteHiddenKeys(const std::vector<std::string>& a_keys);

    // [Durations]: own display time per hide key, set on the History page;
    // same format as [Hidden] plus TimeN (seconds).
    std::map<std::string, float> ReadDurations();
    void                         WriteDurations(const std::map<std::string, float>& a_durations);
}
