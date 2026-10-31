#include "Capture.h"

#include "CenterNotifications.h"
#include "IconFont.h"
#include "Settings.h"
#include "Text.h"
#include "Translation.h"

namespace
{
    std::mutex                            g_lock;
    std::deque<NT::Capture::Entry>        g_entries;
    std::deque<NT::Capture::HistoryEntry> g_history;
    std::uint64_t                         g_nextID = 1;
    std::set<std::string>                 g_hidden;     // hide keys, see NT::Markup::LineHideKey
    std::map<std::string, float>          g_durations;  // own display time per hide key
    std::atomic<bool>                     g_durationsChanged{ false };  // not written to the INI yet

    // Caller holds g_lock.
    bool IsHidden(std::string_view a_raw)
    {
        return !g_hidden.empty() && g_hidden.contains(NT::Markup::LineHideKey(a_raw));
    }

    // Caller holds g_lock.
    std::optional<float> DurationFor(std::string_view a_raw)
    {
        if (g_durations.empty()) {
            return std::nullopt;
        }
        const auto it = g_durations.find(NT::Markup::LineHideKey(a_raw));
        return it != g_durations.end() ? std::optional{ it->second } : std::nullopt;
    }

    // Caller holds g_lock. A line that stops being hidden starts over, so it
    // queues and fades in like a new one instead of popping up mid-life.
    // Returns true when it restarted.
    bool UpdateHidden(NT::Capture::Entry& a_entry, bool a_hidden)
    {
        const bool restart = a_entry.hidden && !a_hidden;
        if (restart) {
            a_entry.started = false;
            a_entry.age = 0.0F;
        }
        a_entry.hidden = a_hidden;
        return restart;
    }

    // Caller holds g_lock. After the hide keys changed: restarted lines move
    // to the back as the newest, so in queue mode they wait behind the lines
    // already showing instead of stalling them.
    void RefreshHidden()
    {
        const auto now = std::chrono::steady_clock::now();
        bool       any = false;
        for (auto& entry : g_entries) {
            if (UpdateHidden(entry, IsHidden(entry.raw))) {
                entry.arrival = now;
                any = true;
            }
        }
        if (any) {
            // Lines still waiting in queue mode are unstarted too and stay in
            // their order behind the started ones.
            std::ranges::stable_partition(g_entries, [](const NT::Capture::Entry& a_entry) { return a_entry.started || a_entry.hidden; });
        }
    }

    // Caller holds g_lock.
    void SetHistoryText(NT::Capture::HistoryEntry& a_entry, std::string_view a_raw)
    {
        a_entry.text = NT::Markup::Visible(a_raw);
        a_entry.rgb = NT::Markup::WrappingColor(a_raw);
        a_entry.icons = NT::Markup::Icons(a_raw);
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

    // The text as the Vanilla text field would show it: a lone "$Key" goes
    // through the Scaleform translator (unknown keys stay as sent).
    std::string Resolve(std::string_view a_raw)
    {
        if (a_raw.size() > 1 && a_raw.front() == '$' && a_raw.find(' ') == std::string_view::npos) {
            return NT::Translation::Translate(a_raw, a_raw);
        }
        return std::string(a_raw);
    }

    // Plugin switched off: the Vanilla HUD shows the line, the history
    // still records it.
    void RecordOnly(std::string_view a_raw)
    {
        const auto       resolved = Resolve(a_raw);
        std::scoped_lock lock{ g_lock };
        AddHistory(g_nextID++, resolved);
    }

    // Any thread except under g_lock: whether the player hid this line.
    bool IsHiddenLine(std::string_view a_raw)
    {
        const auto       resolved = Resolve(a_raw);
        std::scoped_lock lock{ g_lock };
        return IsHidden(resolved);
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

    // False for a line with nothing the overlay can draw: no text and no
    // icon IconFont knows (icon fonts off, or the font is missing). Those
    // stay with the Vanilla HUD, which renders the icon font itself.
    bool DrawableResolved(std::string_view a_resolved)
    {
        if (!NT::Markup::Visible(a_resolved).empty()) {
            return true;
        }
        if (!NT::CaptureConfig().drawIconFonts) {
            return false;
        }
        return std::ranges::any_of(NT::Markup::Icons(a_resolved), [](const auto& a_icon) { return NT::IconFont::Has(a_icon.first, a_icon.second); });
    }

    bool Drawable(std::string_view a_raw)
    {
        return DrawableResolved(Resolve(a_raw));
    }

    NT::Merge::Rule MatchRule(const NT::Capture::Entry& a_previous, std::string_view a_raw, const NT::CaptureSettings& a_settings)
    {
        const auto& s = a_settings;
        if (a_previous.raw == a_raw) {
            return NT::Merge::Rule::kNone;  // a repeat, handled by stacking
        }
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

    // The engine's repeat filter in ShowHUDMessage (SE 52050 / AE 52933),
    // see design-notes, Merge rules, "Repeats". We stack repeats ourselves,
    // so it is cleared after each line we take.
    struct RecentMessage
    {
        std::uint32_t hash;
        std::uint32_t timeMs;
    };
    constexpr std::size_t kRecentMessageSlots = 8;

    // Table and its static-init guard right behind it, per runtime.
    struct RepeatFilterIds
    {
        std::uint64_t table;
        std::uint64_t guard;
    };
    // SE: read from the running 1.5.97 code (the exe is encrypted on disk),
    // see design-notes. AE: from the disassembly of 1.7.104.
    constexpr RepeatFilterIds kRepeatFilterSE{ 520091, 520093 };
    constexpr RepeatFilterIds kRepeatFilterAE{ 406616, 406624 };
    // The versions whose Address Library was checked to have both IDs, 64
    // bytes apart. A missing ID would stop the game at startup (NG's lookup
    // does not fail softly), so any other version is left alone.
    constexpr REL::Version kRepeatFilterVersionsSE[] = { { 1, 5, 97, 0 } };
    constexpr REL::Version kRepeatFilterVersionsAE[] = {
        { 1, 6, 317, 0 }, { 1, 6, 318, 0 }, { 1, 6, 323, 0 }, { 1, 6, 342, 0 }, { 1, 6, 353, 0 },
        { 1, 6, 629, 0 }, { 1, 6, 640, 0 }, { 1, 6, 659, 0 },
        { 1, 6, 1130, 0 }, { 1, 6, 1170, 0 }, { 1, 6, 1179, 0 },
        { 1, 7, 99, 0 }, { 1, 7, 104, 0 },
    };

    RecentMessage* g_recentMessages = nullptr;  // set by Install when the layout matches

    void FindEngineRepeatFilter()
    {
        const auto version = REL::Module::get().version();
        std::optional<RepeatFilterIds> ids;
        if (REL::Module::IsAE() && std::ranges::contains(kRepeatFilterVersionsAE, version)) {
            ids = kRepeatFilterAE;
        } else if (REL::Module::IsSE() && std::ranges::contains(kRepeatFilterVersionsSE, version)) {
            ids = kRepeatFilterSE;
        }
        if (!ids) {
            logs::info("Engine repeat filter left alone on {}", version.string());
            return;
        }
        const auto table = REL::ID(ids->table).address();
        if (REL::ID(ids->guard).address() - table != kRecentMessageSlots * sizeof(RecentMessage)) {
            logs::warn("Engine repeat filter: unexpected layout, left alone");
            return;
        }
        g_recentMessages = reinterpret_cast<RecentMessage*>(table);
        logs::info("Engine repeat filter cleared after each taken line on {}", version.string());
    }

    void ClearEngineRepeatFilter()
    {
        if (g_recentMessages && NT::CaptureConfig().stackDuplicates) {
            std::fill_n(g_recentMessages, kRecentMessageSlots, RecentMessage{});
        }
    }

    // Both hooks: hand the line to the overlay; when it is taken, also clear
    // the engine's repeat filter. False: Vanilla shows it.
    bool Take(const char* a_text)
    {
        if (!NT::Capture::Push(a_text)) {
            return false;
        }
        ClearEngineRepeatFilter();
        return true;
    }

    // Another mod hooked HUDMenu::ProcessMessage before us (ChatBox reads
    // notifications there): our "original" is that mod, not Vanilla, and a
    // swallowed line would never reach it. Taken lines are then passed on,
    // and the copy Vanilla queues in the HUD movie is taken out again.
    bool g_passOn = false;

    // Vanilla queues a notification line for the HUD movie in
    // MessagesInstance.MessageArray and shows it on a later frame. Right
    // after the pass-on the line is the last entry there: remove it.
    void DropVanillaLine(RE::IMenu* a_menu, std::string_view a_text)
    {
        RE::GFxValue queue;
        if (!a_menu->uiMovie || !a_menu->uiMovie->GetVariable(&queue, "_root.HUDMovieBaseInstance.MessagesInstance.MessageArray") ||
            !queue.IsArray() || queue.GetArraySize() == 0) {
            return;
        }
        const auto   last = queue.GetArraySize() - 1;
        RE::GFxValue entry;
        if (queue.GetElement(last, &entry) && entry.IsString() && a_text == entry.GetString()) {
            queue.RemoveElement(last);
        } else {
            static bool logged = false;
            if (!logged) {
                logged = true;
                logs::warn("Passed-on notification not found in the HUD's message queue; Vanilla may show it too");
            }
        }
    }

    // The module a code address belongs to; nullptr if none.
    HMODULE ModuleOf(std::uintptr_t a_address)
    {
        HMODULE module = nullptr;
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(a_address), &module);
        return module;
    }

    std::string ModuleName(HMODULE a_module)
    {
        char path[MAX_PATH]{};
        if (!a_module || !::GetModuleFileNameA(a_module, path, MAX_PATH)) {
            return "an unknown module";
        }
        return std::filesystem::path(path).filename().string();
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
                    if (text && NT::CenterNotifications::HandleDebugMessage(text)) {
                        return RE::UI_MESSAGE_RESULTS::kHandled;
                    }
                    if (!NT::CaptureEnabled()) {
                        if (text && *text) {
                            RecordOnly(text);
                        }
                        return func(a_this, a_message);
                    }
                    if (text && *text && !Take(text)) {
                        return func(a_this, a_message);  // icons we cannot draw: Vanilla can
                    }
                    if (g_passOn && text && *text) {
                        const std::string line = text;  // the other mod may change the message
                        const auto        result = func(a_this, a_message);
                        DropVanillaLine(a_this, line);
                        return result;
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
            if (a_this && NT::CaptureEnabled()) {
                auto& queue = a_this->queue;
                for (auto it = queue.begin(); it != queue.end();) {
                    // Lines we cannot draw (only unknown icons) stay for Vanilla,
                    // unless the player hid them (Push swallows those).
                    if (it->type == RE::HUD_MESSAGE_TYPE::kNotification && !it->quest && !it->word &&
                        (Drawable(it->text.c_str()) || IsHiddenLine(it->text.c_str()))) {
                        static bool logged = false;
                        if (!logged) {
                            logged = true;
                            logs::info("Plain notification taken from the HUDNotifications queue (direct sender)");
                        }
                        if (const char* text = it->text.c_str(); text && *text) {
                            Take(text);  // queued lines are always removed, taken or not
                        }
                        // Vanilla plays a queued entry's sound when it shows the
                        // line; the entry is gone after this, so play it here.
                        if (const char* sound = it->sound.c_str(); sound && *sound) {
                            RE::PlaySound(sound);
                        }
                        it = queue.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            if (a_this) {
                NT::CenterNotifications::BeforeUpdate(a_this);
            }
            func(a_this);
            if (a_this) {
                NT::CenterNotifications::AfterUpdate(a_this);
            }
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
        // Inside the game's own module: Vanilla. Anywhere else: a mod that
        // hooked first and expects to see the notifications.
        const auto previous = ModuleOf(ProcessMessageHook::func.address());
        g_passOn = previous != reinterpret_cast<HMODULE>(REL::Module::get().pointer());
        if (g_passOn) {
            logs::info("HUDMenu::ProcessMessage was already hooked by {}: notifications are passed on to it", ModuleName(previous));
        }

        REL::Relocation<std::uintptr_t> notifications{ RE::VTABLE_HUDNotifications[0] };
        UpdateHook::func = notifications.write_vfunc(0x1, UpdateHook::thunk);

        FindEngineRepeatFilter();

        logs::info("Hooked HUDMenu::ProcessMessage and HUDNotifications::Update");
    }

    bool Push(std::string_view a_raw)
    {
        const auto             resolved = Resolve(a_raw);
        const std::string_view raw = resolved;
        if (!DrawableResolved(raw)) {
            std::scoped_lock lock{ g_lock };
            AddHistory(g_nextID++, raw);  // the history lists it either way
            // Hidden stays hidden even when we cannot draw it (icon fonts
            // off or missing): swallow it instead of handing it to Vanilla.
            return IsHidden(raw);
        }

        const auto s = CaptureConfig();
        const auto  now = std::chrono::steady_clock::now();

        std::scoped_lock lock{ g_lock };

        if (!g_entries.empty()) {
            auto&      last = g_entries.back();
            const auto gap = std::chrono::duration<float>(now - last.arrival).count();
            if (gap <= s.mergeWindow && last.count == 1 && last.mergedBy == Merge::Rule::kNone) {
                if (const auto rule = MatchRule(last, raw, s); rule != Merge::Rule::kNone) {
                    last.raw = Merge::Join(last.raw, raw);
                    last.runs = Markup::Parse(last.raw);
                    last.mergedBy = rule;
                    UpdateHidden(last, IsHidden(last.raw));
                    last.duration = DurationFor(last.raw);
                    if (auto* history = FindHistory(last.id)) {
                        SetHistoryText(*history, last.raw);
                    }
                    logs::debug("Merged ({}, {:.0f} ms): {}", RuleName(rule), gap * 1000.0F, Markup::ToUtf8(last.raw));
                    return true;
                }
            }
        }

        // Stack only onto the newest line: a repeat after another text is a
        // new state (Enabled, Disabled, Enabled) and gets its own line, or the
        // oldest line would look like the current state. Shown and hidden
        // lines are looked at separately, so hidden lines in between do not
        // break a run and a hidden spammer still stacks instead of flooding.
        if (s.stackDuplicates) {
            const bool hidden = IsHidden(raw);
            const auto newest = std::ranges::find_if(g_entries.rbegin(), g_entries.rend(),
                [&](const NT::Capture::Entry& a_entry) { return a_entry.hidden == hidden; });
            if (newest != g_entries.rend()) {
                auto& entry = *newest;
                if (entry.raw == raw && entry.age < s.fadeIn + entry.duration.value_or(s.duration)) {
                    ++entry.count;
                    entry.age = std::min(entry.age, s.fadeIn);
                    if (auto* history = FindHistory(entry.id)) {
                        history->count = entry.count;
                        history->time = std::chrono::system_clock::now();
                    }
                    return true;
                }
            }
        }

        Entry entry;
        entry.id = g_nextID++;
        entry.raw = std::string(raw);
        entry.runs = Markup::Parse(entry.raw);
        entry.arrival = now;
        entry.hidden = IsHidden(entry.raw);
        entry.duration = DurationFor(entry.raw);
        logs::debug("Notification #{}: {}", entry.id, Markup::ToUtf8(entry.raw));
        AddHistory(entry.id, entry.raw);
        g_entries.push_back(std::move(entry));

        // Keep memory bounded when the HUD is hidden for a long time.
        while (g_entries.size() > NT::Capture::kMaxPendingEntries) {
            g_entries.pop_front();
        }
        return true;
    }

    void Visit(const std::function<void(std::deque<Entry>&)>& a_visitor)
    {
        std::scoped_lock lock{ g_lock };
        a_visitor(g_entries);
    }

    void ClearActive()
    {
        std::scoped_lock lock{ g_lock };
        if (!g_entries.empty()) {
            logs::info("Dropped {} pending notification(s) of the previous session", g_entries.size());
        }
        g_entries.clear();
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

    void LoadHidden()
    {
        auto             keys = NT::ReadHiddenKeys();
        std::scoped_lock lock{ g_lock };
        g_hidden = { std::make_move_iterator(keys.begin()), std::make_move_iterator(keys.end()) };
    }

    std::set<std::string> HiddenKeys()
    {
        std::scoped_lock lock{ g_lock };
        return g_hidden;
    }

    void SetHidden(const std::string& a_key, bool a_hidden)
    {
        if (a_key.empty()) {
            return;
        }
        std::vector<std::string> keys;
        {
            std::scoped_lock lock{ g_lock };
            if (a_hidden ? !g_hidden.insert(a_key).second : g_hidden.erase(a_key) == 0) {
                return;
            }
            RefreshHidden();
            keys.assign(g_hidden.begin(), g_hidden.end());
        }
        // File I/O outside the lock, so Push on the game thread never waits on it.
        NT::WriteHiddenKeys(keys);
        logs::info("{} in the HUD: {}", a_hidden ? "Hidden" : "Shown again", a_key);
    }

    void ClearHidden()
    {
        {
            std::scoped_lock lock{ g_lock };
            g_hidden.clear();
            RefreshHidden();
        }
        NT::WriteHiddenKeys({});
        logs::info("All hidden lines shown again");
    }

    void LoadDurations()
    {
        auto             durations = NT::ReadDurations();
        std::scoped_lock lock{ g_lock };
        g_durations = std::move(durations);
    }

    std::map<std::string, float> Durations()
    {
        std::scoped_lock lock{ g_lock };
        return g_durations;
    }

    void SetDuration(const std::string& a_key, std::optional<float> a_seconds)
    {
        if (a_key.empty()) {
            return;
        }
        std::scoped_lock lock{ g_lock };
        g_durationsChanged = true;
        if (a_seconds) {
            g_durations[a_key] = *a_seconds;
        } else {
            g_durations.erase(a_key);
        }
        for (auto& entry : g_entries) {
            entry.duration = DurationFor(entry.raw);
        }
    }

    void SaveDurations()
    {
        // File I/O outside the lock, so Push on the game thread never waits on it.
        g_durationsChanged = false;
        const auto durations = Durations();
        NT::WriteDurations(durations);
        logs::info("{} own display time(s) saved", durations.size());
    }

    void SaveDurationsIfChanged()
    {
        if (g_durationsChanged) {
            SaveDurations();
        }
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
