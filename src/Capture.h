#pragma once

#include "Markup.h"
#include "Merge.h"

// Takes the plain text notifications (Debug.Notification, Message.Show
// without a box, ShowHUDMessage from SKSE plugins) away from the Vanilla HUD
// and keeps them for the overlay. Quest, skill and level messages stay Vanilla.
namespace NT::Capture
{
    struct Entry
    {
        std::uint64_t                         id = 0;
        std::string                           raw;    // as sent (merged halves joined)
        std::vector<Markup::Run>              runs;   // parsed raw
        int                                   count = 1;  // identical repeats stacked onto this line
        Merge::Rule                           mergedBy = Merge::Rule::kNone;
        std::chrono::steady_clock::time_point arrival;
        bool                                  started = false;  // false while waiting in queue mode
        float                                 age = 0.0F;       // seconds since it started showing
    };

    // One line of the session history (menu page "History"), kept in memory
    // only so the plugin stays save-neutral.
    struct HistoryEntry
    {
        std::uint64_t                         id = 0;  // matches Entry::id
        std::chrono::system_clock::time_point time;   // real time of arrival
        std::string                           text;   // visible UTF-8, tags removed
        std::optional<std::uint32_t>          rgb;    // wrapping <font color>, if any
        int                                   count = 1;
    };

    inline constexpr std::size_t kHistorySize = 100;

    // Writes the vtable hooks. Called once at kDataLoaded, only when the SKSE
    // Menu Framework is present (without it nothing could draw the text).
    void Install();

    // Main thread: a notification the HUD would have shown.
    void Push(std::string_view a_raw);

    // Overlay thread: exclusive access to the live entries, oldest first.
    void Visit(const std::function<void(std::deque<Entry>&)>& a_visitor);

    // Menu thread: the history, oldest first, and clearing it.
    void VisitHistory(const std::function<void(const std::deque<HistoryEntry>&)>& a_visitor);
    void ClearHistory();

    // Sends a notification through the regular game path (SKSE task), so
    // menu tests go through the same hook as every other sender.
    void SendTest(std::vector<std::string> a_texts);
}
