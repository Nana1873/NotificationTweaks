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
    // Text area of a group's frame in the positioning mode, in HUD units
    // around the clip origin at scale 1 (measured on the Vanilla and
    // Dragonborn UI HUD: the title line sits about 60 units below it).
    inline constexpr float kTextOffsetY = 60.0F;
    inline constexpr float kFrameWidth = 560.0F;
    inline constexpr float kFrameHeight = 60.0F;

    // Game thread, from the HUDNotifications::Update hook, around the
    // Vanilla update: an entry that left the queue started showing.
    void BeforeUpdate(RE::HUDNotifications* a_this);
    void AfterUpdate(RE::HUDNotifications* a_this);

    // Where the clip's Vanilla origin is on screen and how many screen
    // pixels one HUD unit is; measured on the game thread about once a
    // second. Invalid until the HUD has updated once.
    struct Geometry
    {
        bool  valid = false;
        float originX = 0.0F;
        float originY = 0.0F;
        float pxPerUnit = 1.0F;
    };
    Geometry GetGeometry();

    // Any thread: shows a sample popup (Vanilla location style, a_text) with
    // a_group's placement, queued like any other center notification.
    void Preview(CenterGroup a_group, std::string a_text);

    // Test hook (VerboseLog only): a notification "NTDEBUG:<type>:<text>"
    // becomes a HUD message of that HUD_MESSAGE_TYPE, sent the way Dynamic
    // Location Pop-ups sends its popups. True when it was one.
    bool HandleDebugMessage(std::string_view a_text);
}
