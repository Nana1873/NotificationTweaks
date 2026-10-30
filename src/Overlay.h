#pragma once

#include "SKSEMenuFramework.h"

// Draws the captured notifications on the SKSE Menu Framework HUD layer and
// the layout editor (the lines' frame and the center notification boxes).
namespace NT::Overlay
{
    // Registers the HUD element. kDataLoaded, after Capture::Install.
    void Initialize();

    // Layout editor: sample lines and the center notification boxes are
    // shown and can be dragged, resized and reset. Toggled by the menu.
    void SetPositioning(bool a_on);
    bool IsPositioning();

    // Font names of the framework's ImGui atlas, for the menu's font list.
    std::vector<std::string> FontNames();

    // 0xRRGGBB as an ImGui color.
    inline ImGuiMCP::ImVec4 RgbColor(std::uint32_t a_rgb, float a_alpha = 1.0F)
    {
        return { static_cast<float>((a_rgb >> 16) & 0xFF) / 255.0F, static_cast<float>((a_rgb >> 8) & 0xFF) / 255.0F,
            static_cast<float>(a_rgb & 0xFF) / 255.0F, a_alpha };
    }
}
