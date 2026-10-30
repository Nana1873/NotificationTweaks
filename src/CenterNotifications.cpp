#include "CenterNotifications.h"

namespace
{
    using NT::CenterGroup;

    // Game thread only.
    std::optional<RE::GFxValue::DisplayInfo> g_base;  // Vanilla position and scale of the clip, read once
    std::optional<CenterGroup>               g_applied;  // placement currently set, none = Vanilla
    std::size_t                              g_sizeBefore = 0;
    std::optional<RE::HUD_MESSAGE_TYPE>      g_frontBefore;
    std::chrono::steady_clock::time_point    g_lastMeasure;

    // A queued preview: the next location-style entry uses this group (+1, 0 = none).
    std::atomic<int> g_previewGroup{ 0 };

    std::mutex                             g_geometryLock;
    NT::CenterNotifications::Geometry      g_geometry;

    std::optional<CenterGroup> GroupOf(RE::HUD_MESSAGE_TYPE a_type)
    {
        using T = RE::HUD_MESSAGE_TYPE;
        switch (a_type) {
        case T::kLocationDiscovery:
            return CenterGroup::kLocation;
        case T::kQuestStarted:
        case T::kQuestComplete:
        case T::kQuestFailed:
        case T::kObjectiveStarted:
        case T::kObjectiveComplete:
        case T::kObjectiveFailed:
            return CenterGroup::kQuest;
        case T::kSkillIncrease:
            return CenterGroup::kSkill;
        case T::kWordOfPowerLearned:
        case T::kDragonSoulAbsorbed:
            return CenterGroup::kShout;
        default:
            return std::nullopt;
        }
    }

    RE::GPtr<RE::GFxMovieView> HudMovie()
    {
        auto* ui = RE::UI::GetSingleton();
        auto  hud = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
        return hud ? hud->uiMovie : nullptr;
    }

    // Sets the clip to a_group's placement, or back to Vanilla for none.
    void Apply(RE::GFxValue& a_root, std::optional<CenterGroup> a_group)
    {
        if (!g_base || (!a_group && !g_applied)) {
            return;  // re-applied for every entry of a group, so a changed placement shows at once
        }
        const auto                p = a_group ? NT::CaptureConfig().center[static_cast<std::size_t>(*a_group)] : NT::CenterPlacement{};
        RE::GFxValue::DisplayInfo info;
        info.SetPosition(g_base->GetX() + p.x, g_base->GetY() + p.y);
        info.SetScale(g_base->GetXScale() * p.scale, g_base->GetYScale() * p.scale);
        a_root.SetDisplayInfo(info);
        g_applied = a_group;
        logs::debug("Center notifications: {} at {:+.0f}, {:+.0f}, x{:.2f}", a_group ? static_cast<int>(*a_group) : -1, p.x, p.y, p.scale);
    }

    // Clip origin (Vanilla spot) and one HUD unit in screen pixels, through
    // the parent's localToGlobal and the movie's visible frame.
    void Measure(RE::GFxValue& a_root)
    {
        const auto movie = HudMovie();
        if (!movie || !g_base) {
            return;
        }
        RE::GFxValue parent;
        if (!a_root.GetMember("_parent", &parent) || !parent.IsDisplayObject()) {
            return;
        }
        const auto     rect = movie->GetVisibleFrameRect();
        RE::GViewport  viewport;
        movie->GetViewport(&viewport);
        if (rect.right <= rect.left || rect.bottom <= rect.top) {
            return;
        }
        const auto toScreen = [&](double a_x, double a_y) {
            RE::GFxValue point;
            movie->CreateObject(&point);
            point.SetMember("x", RE::GFxValue(a_x));
            point.SetMember("y", RE::GFxValue(a_y));
            parent.Invoke("localToGlobal", nullptr, &point, 1);
            RE::GFxValue x, y;
            point.GetMember("x", &x);
            point.GetMember("y", &y);
            return std::pair{ viewport.left + (x.GetNumber() - rect.left) * viewport.width / (rect.right - rect.left),
                viewport.top + (y.GetNumber() - rect.top) * viewport.height / (rect.bottom - rect.top) };
        };
        const auto [x0, y0] = toScreen(g_base->GetX(), g_base->GetY());
        const auto [x1, y1] = toScreen(g_base->GetX() + 100.0, g_base->GetY());
        NT::CenterNotifications::Geometry geometry;
        geometry.valid = x1 > x0;
        geometry.originX = static_cast<float>(x0);
        geometry.originY = static_cast<float>(y0);
        geometry.pxPerUnit = static_cast<float>((x1 - x0) / 100.0);
        std::scoped_lock lock{ g_geometryLock };
        g_geometry = geometry;
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
        auto& root = a_this->root;
        if (!root.IsDisplayObject()) {
            return;
        }
        if (!g_base) {
            RE::GFxValue::DisplayInfo info;
            if (!root.GetDisplayInfo(&info)) {
                return;
            }
            g_base = info;
        }
        // The front entry left the queue: Vanilla started showing it.
        if (a_this->queue.size() < g_sizeBefore && g_frontBefore) {
            auto group = GroupOf(*g_frontBefore);
            if (const int preview = g_previewGroup.exchange(0); preview > 0 && group == CenterGroup::kLocation) {
                group = static_cast<CenterGroup>(preview - 1);
            }
            Apply(root, group);
        }
        // A changed placement (menu, drag) reaches the next entry; the HUD
        // geometry is measured about once a second for the drag frames.
        if (const auto now = std::chrono::steady_clock::now(); now - g_lastMeasure > std::chrono::seconds(1)) {
            g_lastMeasure = now;
            Measure(root);
        }
    }

    Geometry GetGeometry()
    {
        std::scoped_lock lock{ g_geometryLock };
        return g_geometry;
    }

    void Preview(CenterGroup a_group, std::string a_text)
    {
        SKSE::GetTaskInterface()->AddTask([a_group, text = std::move(a_text)]() {
            auto* queue = RE::UIMessageQueue::GetSingleton();
            auto* strings = RE::InterfaceStrings::GetSingleton();
            if (auto* data = static_cast<RE::HUDData*>(queue->CreateUIMessageData(strings->hudData))) {
                g_previewGroup = static_cast<int>(a_group) + 1;
                data->type = RE::HUD_MESSAGE_TYPE::kLocationDiscovery;
                data->text = text.c_str();
                queue->AddMessage(strings->hudMenu, RE::UI_MESSAGE_TYPE::kUpdate, data);
            }
        });
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
            logs::debug("Test HUD message type {}: {}", type, text);
        }
        return true;
    }
}
