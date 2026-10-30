#include "CenterNotifications.h"

#include "Settings.h"

namespace
{
    using NT::CenterNotifications::Group;

    struct Placement
    {
        double x = 0.0;      // offset from the Vanilla position, stage pixels
        double y = 0.0;
        double scale = 100.0;  // percent of the Vanilla scale
    };

    std::array<Placement, static_cast<std::size_t>(Group::kCount)> g_placements;

    // Vanilla position and scale of the clip, read once.
    std::optional<RE::GFxValue::DisplayInfo> g_base;

    // Queue state before the Vanilla update.
    std::size_t                          g_sizeBefore = 0;
    std::optional<RE::HUD_MESSAGE_TYPE>  g_frontBefore;
    std::optional<Group>                 g_applied;

    std::optional<Group> GroupOf(RE::HUD_MESSAGE_TYPE a_type)
    {
        using T = RE::HUD_MESSAGE_TYPE;
        switch (a_type) {
        case T::kLocationDiscovery:
            return Group::kLocation;
        case T::kQuestStarted:
        case T::kQuestComplete:
        case T::kQuestFailed:
        case T::kObjectiveStarted:
        case T::kObjectiveComplete:
        case T::kObjectiveFailed:
            return Group::kQuest;
        case T::kSkillIncrease:
            return Group::kSkill;
        case T::kWordOfPowerLearned:
        case T::kDragonSoulAbsorbed:
            return Group::kShout;
        default:
            return std::nullopt;
        }
    }

    const char* GroupName(Group a_group)
    {
        switch (a_group) {
        case Group::kLocation:
            return "Location";
        case Group::kQuest:
            return "Quest";
        case Group::kSkill:
            return "Skill";
        case Group::kShout:
            return "Shout";
        default:
            return "?";
        }
    }

    void Apply(RE::GFxValue& a_root, Group a_group)
    {
        if (!a_root.IsDisplayObject()) {
            return;
        }
        if (!g_base) {
            RE::GFxValue::DisplayInfo info;
            if (!a_root.GetDisplayInfo(&info)) {
                return;
            }
            g_base = info;
            logs::debug("Center notifications: Vanilla clip at {:.1f}, {:.1f}, scale {:.1f}/{:.1f}", info.GetX(), info.GetY(),
                info.GetXScale(), info.GetYScale());
        }
        const auto&                p = g_placements[static_cast<std::size_t>(a_group)];
        RE::GFxValue::DisplayInfo info;
        info.SetPosition(g_base->GetX() + p.x, g_base->GetY() + p.y);
        info.SetScale(g_base->GetXScale() * p.scale / 100.0, g_base->GetYScale() * p.scale / 100.0);
        a_root.SetDisplayInfo(info);
        g_applied = a_group;
        logs::debug("Center notifications: {} placement {:+.0f}, {:+.0f}, {:.0f}%", GroupName(a_group), p.x, p.y, p.scale);
    }
}

namespace NT::CenterNotifications
{
    void BeforeUpdate(RE::HUDNotifications* a_this)
    {
        g_sizeBefore = a_this->queue.size();
        g_frontBefore = g_sizeBefore > 0 ? std::optional{ a_this->queue.front().type.get() } : std::nullopt;
    }

    void AfterUpdate(RE::HUDNotifications* a_this)
    {
        const auto size = a_this->queue.size();
        if (size != g_sizeBefore) {
            logs::debug("Center notifications: queue {} -> {}, front was {}", g_sizeBefore, size,
                g_frontBefore ? std::to_underlying(*g_frontBefore) : -1);
        }
        // The front entry left the queue: Vanilla started showing it.
        if (size < g_sizeBefore && g_frontBefore) {
            if (const auto group = GroupOf(*g_frontBefore)) {
                Apply(a_this->root, *group);
            }
        }
    }

    void LoadSettings()
    {
        const auto path = std::filesystem::absolute("Data/SKSE/Plugins/NotificationTweaks.ini").string();
        const auto read = [&](const char* a_key, double a_default) {
            char buf[64]{};
            GetPrivateProfileStringA("CenterNotifications", a_key, "", buf, sizeof(buf), path.c_str());
            double     value = a_default;
            const auto end = buf + std::strlen(buf);
            if (std::from_chars(buf, end, value).ptr != end || buf == end) {
                value = a_default;
            }
            return value;
        };
        for (std::size_t i = 0; i < g_placements.size(); ++i) {
            const std::string name = GroupName(static_cast<Group>(i));
            g_placements[i] = { read((name + "X").c_str(), 0.0), read((name + "Y").c_str(), 0.0), read((name + "Scale").c_str(), 100.0) };
        }
    }

    bool HandleDebugMessage(std::string_view a_text)
    {
        constexpr std::string_view kPrefix = "NTDEBUG:";
        if (!NT::Config().verboseLog || !a_text.starts_with(kPrefix)) {
            return false;
        }
        a_text.remove_prefix(kPrefix.size());
        const auto colon = a_text.find(':');
        int        type = 0;
        if (colon == std::string_view::npos || std::from_chars(a_text.data(), a_text.data() + colon, type).ptr != a_text.data() + colon) {
            return false;
        }
        const std::string text(a_text.substr(colon + 1));
        auto*             queue = RE::UIMessageQueue::GetSingleton();
        auto*             strings = RE::InterfaceStrings::GetSingleton();
        if (auto* data = static_cast<RE::HUDData*>(queue->CreateUIMessageData(strings->hudData))) {
            data->type = static_cast<RE::HUD_MESSAGE_TYPE>(type);
            data->text = text.c_str();
            queue->AddMessage(strings->hudMenu, RE::UI_MESSAGE_TYPE::kUpdate, data);
            logs::debug("Debug HUD message type {}: {}", type, text);
        }
        return true;
    }
}
