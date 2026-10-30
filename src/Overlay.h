#pragma once

#include "SKSEMenuFramework.h"

// Draws the captured notifications on the SKSE Menu Framework HUD layer and
// handles the drag-to-position mode.
namespace NT::Overlay
{
    // Registers the HUD element. kDataLoaded, after Capture::Install.
    void Initialize();

    // Positioning mode: sample lines are shown and the block follows the mouse
    // while the left button is held on it. Toggled by the menu.
    void SetPositioning(bool a_on);
    bool IsPositioning();

    // Which center notifications a drag in the positioning mode moves: 0 =
    // all groups together, 1 + CenterGroup = that group alone.
    void SetCenterSelection(int a_selection);
    int  CenterSelection();

    // Font names of the framework's ImGui atlas, for the menu's font list.
    std::vector<std::string> FontNames();

    // 0xRRGGBB as an ImGui color.
    inline ImGuiMCP::ImVec4 RgbColor(std::uint32_t a_rgb, float a_alpha = 1.0F)
    {
        return { static_cast<float>((a_rgb >> 16) & 0xFF) / 255.0F, static_cast<float>((a_rgb >> 8) & 0xFF) / 255.0F,
            static_cast<float>(a_rgb & 0xFF) / 255.0F, a_alpha };
    }
}
