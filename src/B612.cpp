#include "B612.h"

#include "Text.h"

namespace
{
    // B612's clip in the HUD movie, spelled like b612_NotificationManager.psc
    // does (it injects the movie as _root.b612_notificationmanager; the HUD
    // movie's lookup ignores case, checked in game on SE 1.5.97).
    constexpr const char* kSearchItemsPath = "_root.b612_NotificationManager.NotificationManager_mc.searchItems";

    std::optional<std::string> StringMember(const RE::GFxValue& a_object, const char* a_name)
    {
        RE::GFxValue value;
        if (!a_object.GetMember(a_name, &value)) {
            return std::nullopt;
        }
        if (value.IsString()) {
            return std::string(value.GetString());
        }
        if (value.IsStringW()) {
            return NT::Text::ToUtf8(value.GetStringW());
        }
        return std::nullopt;
    }

    // searchItems from the HUD movie, read fresh for every line (mods add
    // items at any time, B612 keeps no copy anywhere else). Empty when B612
    // is not loaded in the HUD.
    std::vector<NT::B612::Item> ReadItems()
    {
        std::vector<NT::B612::Item> items;
        auto* ui = RE::UI::GetSingleton();
        const auto hud = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
        if (!hud || !hud->uiMovie) {
            return items;
        }
        RE::GFxValue array;
        if (!hud->uiMovie->GetVariable(&array, kSearchItemsPath) || !array.IsArray()) {
            return items;
        }
        const auto size = array.GetArraySize();
        items.reserve(size);
        for (std::uint32_t i = 0; i < size; ++i) {
            RE::GFxValue entry;
            if (!array.GetElement(i, &entry) || !entry.IsObject()) {
                continue;
            }
            auto& item = items.emplace_back();
            item.text = StringMember(entry, "text");
            item.before = StringMember(entry, "before");
            item.after = StringMember(entry, "after");
            item.callback = StringMember(entry, "callback").value_or("");
        }
        return items;
    }
}

namespace NT::B612
{
    bool Consume(std::string_view a_line)
    {
        const auto items = ReadItems();
        const auto* item = FindMatch(items, a_line);
        if (!item) {
            return false;
        }
        if (item->callback.empty()) {
            logs::debug("B612 NotificationManager item matched");
        } else {
            logs::debug("B612 NotificationManager item matched (mod event {})", item->callback);
            // skse.SendModEvent(callback, line): numArg 0, no sender.
            SKSE::ModCallbackEvent event{ RE::BSFixedString(item->callback), RE::BSFixedString(a_line), 0.0F, nullptr };
            SKSE::GetModCallbackEventSource()->SendEvent(&event);
        }
        return true;
    }
}
