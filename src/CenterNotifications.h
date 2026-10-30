#pragma once

#include "Settings.h"

// The big notifications in the middle of the screen (location discovered,
// also from Dynamic Location Pop-ups; quests and objectives; skill and level
// increases; words of power). Vanilla draws them one at a time in
// HUDMovieBaseInstance.QuestUpdateBaseInstance, in their Vanilla look; each
// CenterGroup can get its own offset and scale, applied to that clip when an
// entry of the group starts showing.
namespace NT::CenterNotifications
{
    // Game thread, from the HUDNotifications::Update hook, around the
    // Vanilla update: an entry that left the queue started showing.
    void BeforeUpdate(RE::HUDNotifications* a_this);
    void AfterUpdate(RE::HUDNotifications* a_this);

    // Where the clip's own origin is on screen, screen pixels per HUD unit
    // and the HUD's own scale of the clip; measured on the game thread about
    // once a second. Invalid until the HUD has updated once.
    struct Geometry
    {
        bool  valid = false;
        float originX = 0.0F;
        float originY = 0.0F;
        float pxPerUnitX = 1.0F;
        float pxPerUnitY = 1.0F;
        float baseScaleX = 1.0F;
        float baseScaleY = 1.0F;

        // A group's title line on screen at a_place, as {left, top, right,
        // bottom}: the box the layout editor draws. Measured on the Vanilla
        // and Dragonborn UI HUD, the title sits about 60 units below the
        // clip origin.
        std::array<float, 4> TitleRect(const CenterPlacement& a_place) const
        {
            constexpr float kTextOffsetY = 60.0F;
            constexpr float kWidth = 560.0F;
            constexpr float kHeight = 60.0F;
            const float     sx = a_place.scale * baseScaleX * pxPerUnitX;
            const float     sy = a_place.scale * baseScaleY * pxPerUnitY;
            const float     cx = originX + a_place.x * pxPerUnitX;
            const float     cy = originY + a_place.y * pxPerUnitY + kTextOffsetY * sy;
            return { cx - kWidth * 0.5F * sx, cy - kHeight * 0.5F * sy, cx + kWidth * 0.5F * sx, cy + kHeight * 0.5F * sy };
        }
    };
    Geometry GetGeometry();

    // Test hook (VerboseLog only): a notification "NTDEBUG:<type>:<text>"
    // with the location type (26 on SE/AE, kLocationDiscovery) becomes a
    // location popup, sent the way Dynamic Location Pop-ups sends them.
    // True when the text was such a test line.
    bool HandleDebugMessage(std::string_view a_text);
}
