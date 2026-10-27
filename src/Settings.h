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

    struct Settings
    {
        // [General]
        bool enabled = true;  // false: Vanilla HUD shows the notifications again

        // [Layout] PosX/PosY are fractions of the screen and mark the anchor
        // of the block: its top edge, and depending on Align its left edge,
        // horizontal center or right edge.
        float posX = 0.025F;
        float posY = 0.06F;
        Align align = Align::kLeft;
        bool  newestOnTop = false;
        float maxWidth = 0.45F;  // fraction of the screen width; longer lines wrap
        int   maxVisible = 6;

        // [Style]
        std::string font;  // ImGui font name from the SKSE Menu Framework atlas; empty = default
        float       fontSize = 24.0F;
        float       lineSpacing = 4.0F;
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
    };

    Settings& Config();
    void      LoadSettings();
    void      SaveSettings();
}
