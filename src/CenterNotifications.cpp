#include "CenterNotifications.h"

namespace
{
    using NT::CenterGroup;

    constexpr double kProbeUnits = 100.0;  // HUD units between the two points Measure maps to the screen
    constexpr auto   kMeasureInterval = std::chrono::seconds(1);

    // Game thread only.
    std::optional<RE::GFxValue::DisplayInfo> g_base;     // the HUD's own position and scale of the clip
    std::optional<RE::GFxValue::DisplayInfo> g_lastSet;  // what Apply set last
    std::size_t                              g_sizeBefore = 0;
    std::optional<RE::HUD_MESSAGE_TYPE>      g_frontBefore;
    std::chrono::steady_clock::time_point    g_lastMeasure;

    std::mutex                        g_geometryLock;
    NT::CenterNotifications::Geometry g_geometry;

    // An SE/AE message type as the running runtime numbers it (VR shifts
    // every type from kShowSubtitle on by 3).
    RE::HUD_MESSAGE_TYPE Runtime(RE::HUD_MESSAGE_TYPE a_seType)
    {
#ifdef ENABLE_SKYRIM_VR
        return RE::GetHUDMessageType(a_seType);
#else
        return a_seType;
#endif
    }

    std::optional<CenterGroup> GroupOf(RE::HUD_MESSAGE_TYPE a_type)
    {
        using T = RE::HUD_MESSAGE_TYPE;
        constexpr std::pair<T, CenterGroup> kGroups[] = {
            { T::kLocationDiscovery, CenterGroup::kLocation },
            { T::kQuestStarted, CenterGroup::kQuest },
            { T::kQuestComplete, CenterGroup::kQuest },
            { T::kQuestFailed, CenterGroup::kQuest },
            { T::kObjectiveStarted, CenterGroup::kQuest },
            { T::kObjectiveComplete, CenterGroup::kQuest },
            { T::kObjectiveFailed, CenterGroup::kQuest },
            { T::kSkillIncrease, CenterGroup::kSkill },
            { T::kWordOfPowerLearned, CenterGroup::kShout },
            { T::kDragonSoulAbsorbed, CenterGroup::kShout },
        };
        for (const auto& [type, group] : kGroups) {
            if (Runtime(type) == a_type) {
                return group;
            }
        }
        return std::nullopt;
    }

    RE::GPtr<RE::GFxMovieView> HudMovie()
    {
        auto* ui = RE::UI::GetSingleton();
        auto  hud = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
        return hud ? hud->uiMovie : nullptr;
    }

    // Sets the clip to a_group's placement. Re-applied for every entry of a
    // group, so a placement changed in the editor shows with the next one.
    void Apply(RE::GFxValue& a_root, CenterGroup a_group)
    {
        // The clip no longer where we left it (a HUD mod moved it, the movie
        // was reloaded): its current values are the HUD's own again.
        RE::GFxValue::DisplayInfo current;
        if (!a_root.GetDisplayInfo(&current)) {
            return;
        }
        const auto moved = [&](const RE::GFxValue::DisplayInfo& a_set) {
            return std::abs(current.GetX() - a_set.GetX()) > 0.5 || std::abs(current.GetY() - a_set.GetY()) > 0.5 ||
                   std::abs(current.GetXScale() - a_set.GetXScale()) > 0.5;
        };
        if (!g_base || (g_lastSet && moved(*g_lastSet))) {
            g_base = current;
        }
        const auto                p = NT::CaptureConfig().center[static_cast<std::size_t>(a_group)];
        RE::GFxValue::DisplayInfo info;
        info.SetPosition(g_base->GetX() + p.x, g_base->GetY() + p.y);
        info.SetScale(g_base->GetXScale() * p.scale, g_base->GetYScale() * p.scale);
        a_root.SetDisplayInfo(info);
        g_lastSet = info;
        logs::debug("Center notifications: {} at {:+.0f}, {:+.0f}, x{:.2f}", NT::kCenterKeys[static_cast<std::size_t>(a_group)], p.x, p.y, p.scale);
    }

    // Clip origin (the HUD's own spot) and HUD units in screen pixels,
    // through the parent's localToGlobal and the movie's visible frame.
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
        const auto    rect = movie->GetVisibleFrameRect();
        RE::GViewport viewport;
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
        const auto [x1, y1] = toScreen(g_base->GetX() + kProbeUnits, g_base->GetY() + kProbeUnits);
        NT::CenterNotifications::Geometry geometry;
        geometry.valid = x1 > x0 && y1 > y0;
        geometry.originX = static_cast<float>(x0);
        geometry.originY = static_cast<float>(y0);
        geometry.pxPerUnitX = static_cast<float>((x1 - x0) / kProbeUnits);
        geometry.pxPerUnitY = static_cast<float>((y1 - y0) / kProbeUnits);
        geometry.baseScaleX = static_cast<float>(g_base->GetXScale() / 100.0);
        geometry.baseScaleY = static_cast<float>(g_base->GetYScale() / 100.0);
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
        // The front entry left the queue: Vanilla started showing it. Only
        // center entries touch the clip; a plain line Vanilla shows meanwhile
        // must not move a popup that is still on screen.
        if (a_this->queue.size() < g_sizeBefore && g_frontBefore) {
            if (const auto group = GroupOf(*g_frontBefore)) {
                Apply(root, *group);
            }
        }
        // For the editor's boxes.
        if (const auto now = std::chrono::steady_clock::now(); now - g_lastMeasure > kMeasureInterval) {
            g_lastMeasure = now;
            Measure(root);
        }
    }

    Geometry GetGeometry()
    {
        std::scoped_lock lock{ g_geometryLock };
        return g_geometry;
    }

    bool HandleDebugMessage(std::string_view a_text)
    {
        constexpr std::string_view kPrefix = "NTDEBUG:";
        if (!NT::CaptureConfig().verboseLog || !a_text.starts_with(kPrefix)) {
            return false;
        }
        a_text.remove_prefix(kPrefix.size());
        const auto colon = a_text.find(':');
        int        type = 0;
        if (colon == std::string_view::npos || std::from_chars(a_text.data(), a_text.data() + colon, type).ptr != a_text.data() + colon) {
            return false;
        }
        // Location popups only: other center types need a quest or word of
        // power the message cannot carry.
        const auto location = Runtime(RE::HUD_MESSAGE_TYPE::kLocationDiscovery);
        if (type != std::to_underlying(location)) {
            logs::debug("Test HUD message: only type {} (location) is allowed", std::to_underlying(location));
            return true;
        }
        const std::string text(a_text.substr(colon + 1));
        auto*             queue = RE::UIMessageQueue::GetSingleton();
        auto*             strings = RE::InterfaceStrings::GetSingleton();
        if (auto* data = static_cast<RE::HUDData*>(queue->CreateUIMessageData(strings->hudData))) {
            data->type = location;
            data->text = text.c_str();
            queue->AddMessage(strings->hudMenu, RE::UI_MESSAGE_TYPE::kUpdate, data);
            logs::debug("Test HUD message (location): {}", text);
        }
        return true;
    }
}
