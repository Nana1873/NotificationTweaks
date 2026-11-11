#pragma once

// B612 (Nexus 127701) NotificationManager: mods register texts there to
// hide a notification line or to get a mod event for it (Feeding Perk
// Notification, Nexus 128242). B612 does it in ActionScript inside the HUD
// movie, on Vanilla's queue, which never sees the lines we take. So each
// line we take is checked against B612's list first and handled the way
// B612 would.
namespace NT::B612
{
    // One entry of B612's searchItems (b612_notificationmanager.swf,
    // NotificationManager.addItem): a non-empty text is an exact match,
    // otherwise before/after match the start and end of the line.
    struct Item
    {
        std::optional<std::string> text;
        std::optional<std::string> before;
        std::optional<std::string> after;
        std::string                callback;  // mod event to send; empty: only hide
    };

    // NotificationManager.isMatch: the first item that matches a_line, or
    // nullptr. Byte comparison of UTF-8 equals B612's AS2 string compare.
    inline const Item* FindMatch(std::span<const Item> a_items, std::string_view a_line)
    {
        for (const auto& item : a_items) {
            if (item.text) {
                if (*item.text == a_line) {
                    return &item;
                }
            } else if (item.before) {
                // AS2: line.substr(0, before.length) == before and
                // line.substr(line.length - after.length) == after; an
                // undefined after (never set by addItem) never matches.
                if (item.after && a_line.starts_with(*item.before) && a_line.ends_with(*item.after)) {
                    return &item;
                }
            }
        }
        return nullptr;
    }

    // Game side: true when B612 is loaded in the HUD and a_line matches one
    // of its items. Sends the item's mod event (name = callback, string
    // argument = the line), like B612's skse.SendModEvent. HUD (UI) thread.
    bool Consume(std::string_view a_line);
}
