#include "Capture.h"

#include "Settings.h"
#include "Translation.h"

namespace
{
    std::mutex                            g_lock;
    std::deque<NT::Capture::Entry>        g_entries;
    std::deque<NT::Capture::HistoryEntry> g_history;
    std::uint64_t                         g_nextID = 1;

    // Caller holds g_lock.
    void SetHistoryText(NT::Capture::HistoryEntry& a_entry, std::string_view a_raw)
    {
        a_entry.text = NT::Markup::Visible(a_raw);
        a_entry.rgb = NT::Markup::WrappingColor(a_raw);
    }

    // Caller holds g_lock.
    void AddHistory(std::uint64_t a_id, std::string_view a_raw)
    {
        NT::Capture::HistoryEntry entry;
        entry.id = a_id;
        entry.time = std::chrono::system_clock::now();
        SetHistoryText(entry, a_raw);
        g_history.push_back(std::move(entry));
        while (g_history.size() > NT::Capture::kHistorySize) {
            g_history.pop_front();
        }
    }

    // Caller holds g_lock.
    NT::Capture::HistoryEntry* FindHistory(std::uint64_t a_id)
    {
        for (auto it = g_history.rbegin(); it != g_history.rend(); ++it) {
            if (it->id == a_id) {
                return &*it;
            }
        }
        return nullptr;
    }

    // Vanilla display (plugin switched off): the history still records.
    void RecordOnly(std::string_view a_raw)
    {
        std::scoped_lock lock{ g_lock };
        AddHistory(g_nextID++, a_raw);
    }

    const char* RuleName(NT::Merge::Rule a_rule)
    {
        switch (a_rule) {
        case NT::Merge::Rule::kYps:
            return "YPS";
        case NT::Merge::Rule::kBos:
            return "BoS";
        case NT::Merge::Rule::kGeneric:
            return "generic";
        default:
            return "none";
        }
    }

    NT::Merge::Rule MatchRule(const NT::Capture::Entry& a_previous, std::string_view a_raw)
    {
        const auto& s = NT::Config();
        if (s.mergeYps && NT::Merge::IsYpsSplit(a_previous.raw, a_raw, s.ypsLimit)) {
            return NT::Merge::Rule::kYps;
        }
        if (s.mergeBos && NT::Merge::IsBosContinuation(a_previous.raw, a_raw)) {
            return NT::Merge::Rule::kBos;
        }
        if (s.mergeGeneric && NT::Merge::IsGenericContinuation(a_previous.raw, a_raw)) {
            return NT::Merge::Rule::kGeneric;
        }
        return NT::Merge::Rule::kNone;
    }

    // HUDMenu::ProcessMessage: a kUpdate carrying HUDData kNotification is
    // the request to show a notification line. Swallowing it here keeps it
    // out of the Vanilla HUDNotifications queue (and its one-at-a-time delay).
    struct ProcessMessageHook
    {
        static RE::UI_MESSAGE_RESULTS thunk(RE::IMenu* a_this, RE::UIMessage& a_message)
        {
            if (a_message.type == RE::UI_MESSAGE_TYPE::kUpdate && a_message.data) {
                if (const auto* data = skyrim_cast<RE::HUDData*>(a_message.data);
                    data && data->type == RE::HUD_MESSAGE_TYPE::kNotification) {
                    const char* text = data->text.c_str();
                    if (!NT::Config().enabled) {
                        if (text && *text) {
                            RecordOnly(text);
                        }
                        return func(a_this, a_message);
                    }
                    if (text && *text) {
                        NT::Capture::Push(text);
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }
            }
            return func(a_this, a_message);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // HUDNotifications::Update: second net for senders that push straight
    // into the queue instead of posting a UI message. Takes plain
    // notifications out before the Vanilla code would show one.
    struct UpdateHook
    {
        static void thunk(RE::HUDNotifications* a_this)
        {
            if (NT::Config().enabled && a_this) {
                auto& queue = a_this->queue;
                for (auto it = queue.begin(); it != queue.end();) {
                    if (it->type.all(RE::HUD_MESSAGE_TYPE::kNotification) && !it->quest && !it->word) {
                        static bool logged = false;
                        if (!logged) {
                            logged = true;
                            logs::info("Plain notification taken from the HUDNotifications queue (direct sender)");
                        }
                        if (const char* text = it->text.c_str(); text && *text) {
                            NT::Capture::Push(text);
                        }
                        it = queue.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            func(a_this);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };
}

namespace NT::Capture
{
    void Install()
    {
        REL::Relocation<std::uintptr_t> hudMenu{ RE::VTABLE_HUDMenu[0] };
        ProcessMessageHook::func = hudMenu.write_vfunc(0x4, ProcessMessageHook::thunk);

        REL::Relocation<std::uintptr_t> notifications{ RE::VTABLE_HUDNotifications[0] };
        UpdateHook::func = notifications.write_vfunc(0x1, UpdateHook::thunk);

        logs::info("Hooked HUDMenu::ProcessMessage and HUDNotifications::Update");
    }

    void Push(std::string_view a_raw)
    {
        // The Vanilla text field translates a lone "$Key" through the
        // Scaleform translator; keep that (unknown keys stay as sent).
        std::string translated;
        if (a_raw.size() > 1 && a_raw.front() == '$' && a_raw.find(' ') == std::string_view::npos) {
            translated = Translation::Translate(a_raw, a_raw);
        }
        const std::string_view raw = translated.empty() ? a_raw : std::string_view{ translated };

        const auto& s = Config();
        const auto  now = std::chrono::steady_clock::now();

        std::scoped_lock lock{ g_lock };

        if (!g_entries.empty()) {
            auto&      last = g_entries.back();
            const auto gap = std::chrono::duration<float>(now - last.arrival).count();
            if (gap <= s.mergeWindow && last.count == 1 && last.mergedBy == Merge::Rule::kNone) {
                if (const auto rule = MatchRule(last, raw); rule != Merge::Rule::kNone) {
                    last.raw = Merge::Join(last.raw, raw);
                    last.runs = Markup::Parse(last.raw);
                    last.mergedBy = rule;
                    if (auto* history = FindHistory(last.id)) {
                        SetHistoryText(*history, last.raw);
                    }
                    logs::info("Merged ({}, {:.0f} ms): {}", RuleName(rule), gap * 1000.0F, Markup::ToUtf8(last.raw));
                    return;
                }
            }
        }

        if (s.stackDuplicates) {
            for (auto& entry : g_entries) {
                if (entry.raw == raw && entry.age < s.fadeIn + s.duration) {
                    ++entry.count;
                    entry.age = std::min(entry.age, s.fadeIn);
                    if (auto* history = FindHistory(entry.id)) {
                        history->count = entry.count;
                        history->time = std::chrono::system_clock::now();
                    }
                    return;
                }
            }
        }

        Entry entry;
        entry.id = g_nextID++;
        entry.raw = std::string(raw);
        entry.runs = Markup::Parse(entry.raw);
        entry.arrival = now;
        logs::info("Notification #{}: {}", entry.id, Markup::ToUtf8(entry.raw));
        AddHistory(entry.id, entry.raw);
        g_entries.push_back(std::move(entry));

        // Keep memory bounded when the HUD is hidden for a long time.
        while (g_entries.size() > 64) {
            g_entries.pop_front();
        }
    }

    void Visit(const std::function<void(std::deque<Entry>&)>& a_visitor)
    {
        std::scoped_lock lock{ g_lock };
        a_visitor(g_entries);
    }

    void VisitHistory(const std::function<void(const std::deque<HistoryEntry>&)>& a_visitor)
    {
        std::scoped_lock lock{ g_lock };
        a_visitor(g_history);
    }

    void ClearHistory()
    {
        std::scoped_lock lock{ g_lock };
        g_history.clear();
    }

    void SendTest(std::vector<std::string> a_texts)
    {
        SKSE::GetTaskInterface()->AddTask([texts = std::move(a_texts)]() {
            for (const auto& text : texts) {
                RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, false);
            }
        });
    }
}
