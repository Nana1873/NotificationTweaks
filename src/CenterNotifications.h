#pragma once

// The big notifications in the middle of the screen (location discovered,
// quest started/completed and objectives, skill and level increases, words
// of power): Vanilla draws them one at a time in
// HUDMovieBaseInstance.QuestUpdateBaseInstance. Each group can get its own
// offset and scale; the clip is moved when an entry of that group starts.
namespace NT::CenterNotifications
{
    enum class Group
    {
        kLocation,
        kQuest,
        kSkill,
        kShout,
        kCount
    };

    // Game thread, from the HUDNotifications::Update hook: a_this's queue
    // before and after the Vanilla update, so the entry that started is known.
    void BeforeUpdate(RE::HUDNotifications* a_this);
    void AfterUpdate(RE::HUDNotifications* a_this);

    // Prototype: offsets (stage pixels) and scale (percent) from
    // [CenterNotifications] in the INI, read at startup.
    void LoadSettings();

    // Debug (VerboseLog only): "NTDEBUG:<type>:<text>" sent as a notification
    // becomes a HUD message of that HUD_MESSAGE_TYPE, the way Dynamic
    // Location Pop-ups sends its location popups. True when it was one.
    bool HandleDebugMessage(std::string_view a_text);
}
