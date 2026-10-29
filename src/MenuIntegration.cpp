#include "MenuIntegration.h"

#include "Capture.h"
#include "IconFont.h"
#include "Merge.h"
#include "Overlay.h"
#include "SKSEMenuFramework.h"
#include "Settings.h"
#include "Text.h"
#include "Translation.h"

namespace
{
    constexpr auto kSectionName = "Notification Tweaks";

    // Two-click confirmation for the reset and "show all hidden" buttons.
    constexpr double kConfirmSeconds = 4.0;
    double           g_confirmUntil = 0.0;
    double           g_showAllConfirmUntil = 0.0;

    // Font Awesome solid glyphs from the Menu Framework (U+F06E, U+F070).
    constexpr auto kIconEye = "\xEF\x81\xAE";
    constexpr auto kIconEyeSlash = "\xEF\x81\xB0";

    SKSEMenuFramework::Model::WindowInterface* g_positionWindow = nullptr;

    // Ctrl+Click lets the player type a value; keep it inside the range.
    constexpr auto kClamp = ImGuiMCP::ImGuiSliderFlags_AlwaysClamp;

    // Saves after a slider or drag was released, or right away for
    // checkboxes and combos (a_changedNow).
    void Commit(bool a_changedNow = false)
    {
        if (a_changedNow || ImGuiMCP::IsItemDeactivatedAfterEdit()) {
            NT::SaveSettings();
        }
    }

    void Tooltip(const std::string& a_text)
    {
        if (ImGuiMCP::IsItemHovered(ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGuiMCP::SetTooltip("%s", a_text.c_str());
        }
    }

    // Slider showing a 0..1 fraction as percent.
    void PercentSlider(const std::string& a_label, const char* a_id, float& a_value, NT::Range<float> a_range)
    {
        float percent = a_value * 100.0F;
        if (ImGuiMCP::SliderFloat((a_label + a_id).c_str(), &percent, a_range.min * 100.0F, a_range.max * 100.0F, "%.1f%%", kClamp)) {
            a_value = percent / 100.0F;
        }
        Commit();
    }

    void SecondsSlider(const std::string& a_label, const char* a_id, float& a_value, NT::Range<float> a_range)
    {
        ImGuiMCP::SliderFloat((a_label + a_id).c_str(), &a_value, a_range.min, a_range.max, "%.2f s", kClamp);
        Commit();
    }

    bool Combo(const std::string& a_label, const char* a_id, int& a_index, const std::vector<const std::string*>& a_items)
    {
        bool changed = false;
        const auto preview = a_index >= 0 && a_index < static_cast<int>(a_items.size()) ? a_items[static_cast<std::size_t>(a_index)]->c_str() : "";
        if (ImGuiMCP::BeginCombo((a_label + a_id).c_str(), preview)) {
            for (int i = 0; i < static_cast<int>(a_items.size()); ++i) {
                if (ImGuiMCP::Selectable(a_items[static_cast<std::size_t>(i)]->c_str(), i == a_index)) {
                    a_index = i;
                    changed = true;
                }
            }
            ImGuiMCP::EndCombo();
        }
        return changed;
    }

    void RenderFontCombo()
    {
        const auto& t = NT::Translation::S();
        auto&       s = NT::Config();
        const auto  names = NT::Overlay::FontNames();
        const auto  preview = s.font.empty() ? t.fontDefault : s.font;
        if (ImGuiMCP::BeginCombo((t.font + "##font").c_str(), preview.c_str())) {
            if (ImGuiMCP::Selectable(t.fontDefault.c_str(), s.font.empty())) {
                s.font.clear();
                NT::SaveSettings();
            }
            for (const auto& name : names) {
                if (ImGuiMCP::Selectable(name.c_str(), name == s.font)) {
                    s.font = name;
                    NT::SaveSettings();
                }
            }
            ImGuiMCP::EndCombo();
        }
    }

    void OpenPositioning()
    {
        if (!g_positionWindow) {
            return;
        }
        NT::Overlay::SetPositioning(true);
        g_positionWindow->IsOpen = true;
        if (auto* main = SKSEMenuFramework::GetMainWindow()) {
            main->IsOpen = false;
        }
    }

    void ClosePositioning()
    {
        NT::Overlay::SetPositioning(false);
        NT::SaveSettings();
        if (g_positionWindow) {
            g_positionWindow->IsOpen = false;
        }
        if (auto* main = SKSEMenuFramework::GetMainWindow()) {
            main->IsOpen = true;
        }
    }

    // Small window shown while dragging: hint, fine sliders and Done.
    void __stdcall RenderPositionWindow()
    {
        const auto& t = NT::Translation::S();
        auto&       s = NT::Config();
        auto*       viewport = ImGuiMCP::GetMainViewport();
        ImGuiMCP::SetNextWindowPos({ viewport->Size.x * 0.5F, viewport->Size.y * 0.82F }, ImGuiMCP::ImGuiCond_Appearing, { 0.5F, 0.5F });
        bool open = true;
        if (ImGuiMCP::Begin((t.posWindowTitle + "###NTPosition").c_str(), &open,
                ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize | ImGuiMCP::ImGuiWindowFlags_NoCollapse)) {
            ImGuiMCP::TextUnformatted(t.posWindowHint.c_str());
            PercentSlider(t.posX, "##posx", s.posX, NT::Limits::kPos);
            PercentSlider(t.posY, "##posy", s.posY, NT::Limits::kPos);
            int align = static_cast<int>(s.align);
            if (Combo(t.align, "##palign", align, { &t.alignLeft, &t.alignCenter, &t.alignRight })) {
                s.align = static_cast<NT::Align>(align);
                NT::SaveSettings();
            }
            if (ImGuiMCP::Button((t.done + "##posdone").c_str())) {
                open = false;
            }
        }
        ImGuiMCP::End();
        if (!open) {
            ClosePositioning();
        }
    }

    void RenderResetDisplay()
    {
        const auto& t = NT::Translation::S();
        const auto  now = ImGuiMCP::GetTime();
        const bool  armed = now < g_confirmUntil;
        if (ImGuiMCP::Button(((armed ? t.resetConfirm : t.resetDisplay) + "##reset").c_str())) {
            if (armed) {
                g_confirmUntil = 0.0;
                NT::ResetDisplaySettings();
            } else {
                g_confirmUntil = now + kConfirmSeconds;
            }
        }
    }

    void __stdcall RenderDisplay()
    {
        const auto& t = NT::Translation::S();
        auto&       s = NT::Config();

        Commit(ImGuiMCP::Checkbox((t.enabled + "##enabled").c_str(), &s.enabled));
        Tooltip(t.enabledTip);

        ImGuiMCP::SeparatorText(t.headPosition.c_str());
        if (ImGuiMCP::Button((t.dragButton + "##drag").c_str())) {
            OpenPositioning();
        }
        Tooltip(t.dragTip);
        int align = static_cast<int>(s.align);
        if (Combo(t.align, "##align", align, { &t.alignLeft, &t.alignCenter, &t.alignRight })) {
            s.align = static_cast<NT::Align>(align);
            NT::SaveSettings();
        }

        ImGuiMCP::SeparatorText(t.headStyle.c_str());
        RenderFontCombo();
        ImGuiMCP::SliderFloat((t.fontSize + "##fontsize").c_str(), &s.fontSize, NT::Limits::kFontSize.min, NT::Limits::kFontSize.max, "%.0f px", kClamp);
        Commit();
        Tooltip(t.fontSizeTip);
        ImGuiMCP::ColorEdit4((t.textColor + "##color").c_str(), s.textColor);
        Commit();
        int effect = static_cast<int>(s.effect);
        if (Combo(t.effect, "##effect", effect, { &t.effectNone, &t.effectShadow, &t.effectOutline })) {
            s.effect = static_cast<NT::TextEffect>(effect);
            NT::SaveSettings();
        }

        ImGuiMCP::SeparatorText(t.headTiming.c_str());
        Commit(ImGuiMCP::Checkbox((t.immediate + "##immediate").c_str(), &s.immediate));
        Tooltip(t.immediateTip);
        if (!s.immediate) {
            SecondsSlider(t.queueDelay, "##queuedelay", s.queueDelay, NT::Limits::kQueueDelay);
        }
        SecondsSlider(t.duration, "##duration", s.duration, NT::Limits::kDuration);
        Commit(ImGuiMCP::Checkbox((t.stack + "##stack").c_str(), &s.stackDuplicates));

        // Values most players set once or never.
        ImGuiMCP::Spacing();
        if (ImGuiMCP::CollapsingHeader((t.headAdvanced + "##advanced").c_str())) {
            PercentSlider(t.posX, "##posx", s.posX, NT::Limits::kPos);
            PercentSlider(t.posY, "##posy", s.posY, NT::Limits::kPos);
            Commit(ImGuiMCP::Checkbox((t.newestOnTop + "##newest").c_str(), &s.newestOnTop));
            Commit(ImGuiMCP::Checkbox((t.showInMenus + "##inmenus").c_str(), &s.showInMenus));
            Tooltip(t.showInMenusTip);
            PercentSlider(t.maxWidth, "##maxwidth", s.maxWidth, NT::Limits::kMaxWidth);
            ImGuiMCP::SliderInt((t.maxVisible + "##maxvisible").c_str(), &s.maxVisible, NT::Limits::kMaxVisible.min, NT::Limits::kMaxVisible.max, "%d", kClamp);
            Commit();
            ImGuiMCP::SliderFloat((t.lineSpacing + "##spacing").c_str(), &s.lineSpacing, NT::Limits::kLineSpacing.min, NT::Limits::kLineSpacing.max, "%.0f px", kClamp);
            Commit();
            PercentSlider(t.background, "##background", s.backgroundAlpha, NT::Limits::kBackground);
            SecondsSlider(t.fadeIn, "##fadein", s.fadeIn, NT::Limits::kFadeIn);
            SecondsSlider(t.fadeOut, "##fadeout", s.fadeOut, NT::Limits::kFadeOut);
            RenderResetDisplay();
        }

        ImGuiMCP::SeparatorText(t.headTest.c_str());
        if (ImGuiMCP::Button((t.test1 + "##test1").c_str())) {
            NT::Capture::SendTest({ t.testText });
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button((t.testMany + "##testmany").c_str())) {
            std::vector<std::string> texts;
            for (int i = 1; i <= 4; ++i) {
                texts.push_back(std::format("{} {}", t.testManyText, i));
            }
            NT::Capture::SendTest(std::move(texts));
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::TextDisabled("%s", t.footer.c_str());
    }

    // Splits a_text exactly like ypsThoughts.psc does, for the test button.
    std::vector<std::string> YpsSplit(const std::string& a_text)
    {
        const auto split = NT::Merge::YpsSplitPos(a_text);
        if (split >= a_text.size()) {
            return { a_text };
        }
        return { a_text.substr(0, split), a_text.substr(split + 1) };
    }

    void __stdcall RenderCompat()
    {
        const auto& t = NT::Translation::S();
        auto&       s = NT::Config();

        ImGuiMCP::TextWrapped("%s", t.compatHint.c_str());
        ImGuiMCP::Spacing();
        Commit(ImGuiMCP::Checkbox((t.mergeYps + "##yps").c_str(), &s.mergeYps));
        Tooltip(t.mergeYpsTip);
        Commit(ImGuiMCP::Checkbox((t.mergeBos + "##bos").c_str(), &s.mergeBos));
        Tooltip(t.mergeBosTip);
        Commit(ImGuiMCP::Checkbox((t.mergeGeneric + "##generic").c_str(), &s.mergeGeneric));
        Tooltip(t.mergeGenericTip);
        if (const bool changed = ImGuiMCP::Checkbox((t.drawIconFonts + "##iconfonts").c_str(), &s.drawIconFonts); changed) {
            // Load before publishing the setting, so the game thread never
            // finds the option on with the fonts still unread.
            if (s.drawIconFonts) {
                NT::IconFont::Preload();
            }
            NT::SaveSettings();
        }
        Tooltip(t.drawIconFontsTip);
        if (s.drawIconFonts) {
            const auto found = NT::IconFont::Found();  // cached after the first load
            if (found.empty()) {
                ImGuiMCP::TextDisabled("    %s", t.iconFontsNone.c_str());
            } else {
                ImGuiMCP::TextDisabled("    %s", t.iconFontsFound.c_str());
                for (const auto& [face, glyphs] : found) {
                    ImGuiMCP::TextDisabled("      %s (%zu %s)", face.c_str(), glyphs, t.iconFontGlyphs.c_str());
                }
            }
        }
        SecondsSlider(t.mergeWindow, "##mergewindow", s.mergeWindow, NT::Limits::kMergeWindow);

        ImGuiMCP::SeparatorText(t.headTest.c_str());
        if (ImGuiMCP::Button((t.testYps + "##testyps").c_str())) {
            NT::Capture::SendTest(YpsSplit(t.testYpsText));
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button((t.testBos + "##testbos").c_str())) {
            NT::Capture::SendTest({ "<font color=\"#FF00FF\">" + t.testBosFirst + "</font>", "<font color=\"#FF00FF\">" + t.testBosSecond + "</font>" });
        }

        ImGuiMCP::Spacing();
        ImGuiMCP::TextDisabled("%s", t.footer.c_str());
    }

    std::string LocalTime(std::chrono::system_clock::time_point a_time)
    {
        const auto seconds = std::chrono::system_clock::to_time_t(a_time);
        std::tm    local{};
        localtime_s(&local, &seconds);
        return std::format("{:02}:{:02}:{:02}", local.tm_hour, local.tm_min, local.tm_sec);
    }

    // Eye in front of a history line: hides every line with the same hide
    // key in the HUD, or shows them again.
    void HideToggle(const std::string& a_key, bool a_hidden, std::uint64_t a_id)
    {
        const auto& t = NT::Translation::S();
        // Hiding only acts on the plugin's own lines; with the plugin off the
        // Vanilla HUD shows everything.
        const bool enabled = NT::CaptureEnabled();
        ImGuiMCP::BeginDisabled(!enabled);
        FontAwesome::PushSolid();
        // "###": the ID stays the same when the icon flips.
        const bool clicked = ImGuiMCP::SmallButton(std::format("{}###hide{}", a_hidden ? kIconEyeSlash : kIconEye, a_id).c_str());
        FontAwesome::Pop();
        ImGuiMCP::EndDisabled();
        Tooltip(!enabled ? t.historyHideOff : a_hidden ? t.historyUnhide : t.historyHide);
        if (clicked) {
            NT::Capture::SetHidden(a_key, !a_hidden);
        }
    }

    // Page "History": the session's notifications, newest first, in their
    // own color; merged halves appear as the one line they became. Hidden
    // lines are grey with a closed eye.
    void __stdcall RenderHistory()
    {
        const auto& t = NT::Translation::S();
        static char filter[128]{};

        const auto hidden = NT::Capture::HiddenKeys();

        ImGuiMCP::TextDisabled("%s", t.historyHint.c_str());
        ImGuiMCP::InputTextWithHint("##historyfilter", t.historyFilter.c_str(), filter, sizeof(filter));
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button((t.historyClear + "##historyclear").c_str())) {
            NT::Capture::ClearHistory();
        }
        if (hidden.empty()) {
            g_showAllConfirmUntil = 0.0;
        } else {
            ImGuiMCP::SameLine();
            const auto now = ImGuiMCP::GetTime();
            const bool armed = now < g_showAllConfirmUntil;
            if (ImGuiMCP::Button(((armed ? t.resetConfirm : std::format("{} ({})", t.historyShowAll, hidden.size())) + "###showall").c_str())) {
                if (armed) {
                    g_showAllConfirmUntil = 0.0;
                    NT::Capture::ClearHidden();
                } else {
                    g_showAllConfirmUntil = now + kConfirmSeconds;
                }
            }
        }
        ImGuiMCP::Separator();

        const auto needle = NT::Text::Lower(filter);
        if (ImGuiMCP::BeginChild("##historylist", { 0.0F, 0.0F })) {
            bool                                  any = false;
            std::deque<NT::Capture::HistoryEntry> history;
            NT::Capture::VisitHistory([&](const std::deque<NT::Capture::HistoryEntry>& a_history) { history = a_history; });
            for (auto it = history.rbegin(); it != history.rend(); ++it) {
                if (!needle.empty() && NT::Text::Lower(it->text).find(needle) == std::string::npos) {
                    continue;
                }
                any = true;
                if (it->text.empty()) {  // only icons: nothing to read or hide by
                    ImGuiMCP::TextDisabled("%s  %s", LocalTime(it->time).c_str(), t.historyIconOnly.c_str());
                    continue;
                }
                const auto key = NT::Text::HideKey(it->text);
                const bool isHidden = hidden.contains(key);
                HideToggle(key, isHidden, it->id);
                ImGuiMCP::SameLine();
                ImGuiMCP::TextDisabled("%s", LocalTime(it->time).c_str());
                ImGuiMCP::SameLine();
                const auto line = it->count > 1 ? std::format("{} (x{})", it->text, it->count) : it->text;
                if (isHidden) {
                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, *ImGuiMCP::GetStyleColorVec4(ImGuiMCP::ImGuiCol_TextDisabled));
                    ImGuiMCP::TextWrapped("%s", line.c_str());
                    ImGuiMCP::PopStyleColor();
                    continue;
                }
                if (it->rgb) {
                    ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, NT::Overlay::RgbColor(*it->rgb));
                }
                ImGuiMCP::TextWrapped("%s", line.c_str());
                if (it->rgb) {
                    ImGuiMCP::PopStyleColor();
                }
            }
            if (!any) {
                ImGuiMCP::TextDisabled("%s", t.historyEmpty.c_str());
            }
        }
        ImGuiMCP::EndChild();
    }

    // Leaving the menu ends positioning even when the window was not closed
    // through Done.
    void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_type)
    {
        if (a_type == SKSEMenuFramework::Model::kCloseMenu && NT::Overlay::IsPositioning() && g_positionWindow && !g_positionWindow->IsOpen) {
            NT::Overlay::SetPositioning(false);
            NT::SaveSettings();
        }
    }
}

namespace NT::MenuIntegration
{
    void Initialize()
    {
        const auto& t = NT::Translation::S();
        SKSEMenuFramework::SetSection(kSectionName);
        SKSEMenuFramework::AddSectionItem(t.pageDisplay, RenderDisplay);
        SKSEMenuFramework::AddSectionItem(t.pageCompat, RenderCompat);
        SKSEMenuFramework::AddSectionItem(t.pageHistory, RenderHistory);
        g_positionWindow = SKSEMenuFramework::AddWindow(RenderPositionWindow, true);
        SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0F);
        logs::info("Registered SKSE Menu Framework section '{}' (framework v{}), pages '{}', '{}', '{}'", kSectionName,
            SKSEMenuFramework::GetMenuFrameworkVersion(), t.pageDisplay, t.pageCompat, t.pageHistory);
    }
}
