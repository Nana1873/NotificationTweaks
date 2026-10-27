#include "Capture.h"

#include "Settings.h"

namespace
{
    std::mutex                    g_lock;
    std::deque<NT::Capture::Entry> g_entries;
    std::uint64_t                 g_nextID = 1;

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
        if (s.mergeYps && NT::Merge::IsYpsSplit(a_previous.raw, a_raw)) {
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
            if (NT::Config().enabled && a_message.type == RE::UI_MESSAGE_TYPE::kUpdate && a_message.data) {
                if (const auto* data = skyrim_cast<RE::HUDData*>(a_message.data);
                    data && data->type == RE::HUD_MESSAGE_TYPE::kNotification) {
                    const char* text = data->text.c_str();
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
        const auto& s = Config();
        const auto  now = std::chrono::steady_clock::now();

        std::scoped_lock lock{ g_lock };

        if (!g_entries.empty()) {
            auto&      last = g_entries.back();
            const auto gap = std::chrono::duration<float>(now - last.arrival).count();
            if (gap <= s.mergeWindow && last.count == 1 && last.mergedBy == Merge::Rule::kNone) {
                if (const auto rule = MatchRule(last, a_raw); rule != Merge::Rule::kNone) {
                    last.raw = Merge::Join(last.raw, a_raw);
                    last.runs = Markup::Parse(last.raw);
                    last.mergedBy = rule;
                    logs::info("Merged ({}, {:.0f} ms): {}", RuleName(rule), gap * 1000.0F, Markup::ToUtf8(last.raw));
                    return;
                }
            }
        }

        if (s.stackDuplicates) {
            for (auto& entry : g_entries) {
                if (entry.raw == a_raw && entry.age < s.fadeIn + s.duration) {
                    ++entry.count;
                    entry.age = std::min(entry.age, s.fadeIn);
                    return;
                }
            }
        }

        Entry entry;
        entry.id = g_nextID++;
        entry.raw = std::string(a_raw);
        entry.runs = Markup::Parse(entry.raw);
        entry.arrival = now;
        logs::info("Notification #{}: {}", entry.id, Markup::ToUtf8(entry.raw));
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

    void SendTest(std::vector<std::string> a_texts)
    {
        SKSE::GetTaskInterface()->AddTask([texts = std::move(a_texts)]() {
            for (const auto& text : texts) {
                RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, false);
            }
        });
    }
}
