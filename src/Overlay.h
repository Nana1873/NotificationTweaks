#pragma once

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

    // Font names of the framework's ImGui atlas, for the menu's font list.
    std::vector<std::string> FontNames();
}
