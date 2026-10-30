#include "MenuIntegration.h"

#include "Capture.h"
#include "CenterNotifications.h"
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

    // Center notifications: size, preview and reset per group; they move in
    // the positioning mode.
    void RenderCenter()
    {
        const auto& t = NT::Translation::S();
        auto&       s = NT::Config();
        ImGuiMCP::SeparatorText(t.headCenter.c_str());
        ImGuiMCP::PushTextWrapPos(0.0F);
        ImGuiMCP::TextDisabled("%s", t.centerHint.c_str());
        ImGuiMCP::PopTextWrapPos();
        const std::string* names[NT::kCenterGroups] = { &t.centerLocation, &t.centerQuest, &t.centerSkill, &t.centerShout };
        for (std::size_t i = 0; i < NT::kCenterGroups; ++i) {
            auto&       place = s.center[i];
            const auto  id = std::to_string(i);
            ImGuiMCP::TextUnformatted(names[i]->c_str());
            float percent = place.scale * 100.0F;
            if (ImGuiMCP::SliderFloat((t.centerSize + "##csize" + id).c_str(), &percent, NT::Limits::kCenterScale.min * 100.0F,
                    NT::Limits::kCenterScale.max * 100.0F, "%.0f%%", kClamp)) {
                place.scale = percent / 100.0F;
            }
            Commit();
            if (ImGuiMCP::Button((t.centerPreview + "##cpreview" + id).c_str())) {
                NT::CenterNotifications::Preview(static_cast<NT::CenterGroup>(i), *names[i]);
            }
            Tooltip(t.centerPreviewTip);
            ImGuiMCP::SameLine();
            ImGuiMCP::BeginDisabled(place == NT::CenterPlacement{});
            if (ImGuiMCP::Button((t.centerReset + "##creset" + id).c_str())) {
                place = {};
                NT::SaveSettings();
            }
            ImGuiMCP::EndDisabled();
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

        RenderCenter();

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

    void __stdcall RenderCompat()
    {
        const auto& t = NT::Translation::S();
        auto&       s = NT::Config();

        ImGuiMCP::SeparatorText(t.headIconFonts.c_str());
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
                    const std::string_view name = face.starts_with('$') ? std::string_view(face).substr(1) : std::string_view(face);
                    ImGuiMCP::TextDisabled("      %.*s (%zu %s)", static_cast<int>(name.size()), name.data(), glyphs, t.iconFontGlyphs.c_str());
                }
            }
        }

        ImGuiMCP::SeparatorText(t.headMerge.c_str());
        ImGuiMCP::TextWrapped("%s", t.compatHint.c_str());
        ImGuiMCP::Spacing();
        Commit(ImGuiMCP::Checkbox((t.mergeYps + "##yps").c_str(), &s.mergeYps));
        Tooltip(t.mergeYpsTip);
        Commit(ImGuiMCP::Checkbox((t.mergeBos + "##bos").c_str(), &s.mergeBos));
        Tooltip(t.mergeBosTip);
        Commit(ImGuiMCP::Checkbox((t.mergeGeneric + "##generic").c_str(), &s.mergeGeneric));
        Tooltip(t.mergeGenericTip);
        SecondsSlider(t.mergeWindow, "##mergewindow", s.mergeWindow, NT::Limits::kMergeWindow);

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

    // An invisible, inactive eye: rows without one keep the same columns.
    void EyeSpacer(std::uint64_t a_id)
    {
        ImGuiMCP::BeginDisabled(true);
        ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_Alpha, 0.0F);
        FontAwesome::PushSolid();
        ImGuiMCP::SmallButton(std::format("{}###hide{}", kIconEye, a_id).c_str());
        FontAwesome::Pop();
        ImGuiMCP::PopStyleVar();
        ImGuiMCP::EndDisabled();
    }

    // Icon-font glyphs of a history line, one text line high, on the current
    // line; with a_textFollows the text continues on the same line after
    // them. Returns how many were drawn.
    int HistoryIcons(const NT::Capture::HistoryEntry& a_entry, const ImGuiMCP::ImVec4& a_tint, bool a_textFollows)
    {
        if (!NT::Config().drawIconFonts) {
            return 0;
        }
        const float height = ImGuiMCP::GetTextLineHeight();
        int         drawn = 0;
        for (const auto& [face, code] : a_entry.icons) {
            if (const auto icon = NT::IconFont::Get(face, code, height)) {
                if (drawn > 0) {
                    ImGuiMCP::SameLine(0.0F, 0.0F);  // glyphs packed, as in the overlay
                }
                ImGuiMCP::Image(icon->texture, { icon->aspect * height, height }, icon->uv0, icon->uv1, a_tint);
                ++drawn;
            }
        }
        if (drawn > 0 && a_textFollows) {
            ImGuiMCP::SameLine();
        }
        return drawn;
    }

    constexpr float kDefaultTimeAlpha = 0.45F;  // dimmed, still readable
    constexpr float kTimeSliderEms = 5.0F;      // fits "30.0 s" and "Standard"
    constexpr float kTimeStep = 0.1F;           // as shown ("%.1f s") and saved ("{:.1f}")

    float DurationSliderWidth()
    {
        return ImGuiMCP::GetFontSize() * kTimeSliderEms;
    }

    // Own display time of a history line, at the right edge of its row.
    // Always there (reachable with a gamepad too); dimmed while the line
    // uses the general setting, so lines with their own time stand out.
    // Right-click goes back to the general setting. a_defaultLabel is the
    // "Default" text, escaped for use as a format string.
    void DurationSlider(const std::string& a_key, std::optional<float> a_own, bool a_hidden, bool a_dim, const std::string& a_defaultLabel,
        std::uint64_t a_id)
    {
        const auto& t = NT::Translation::S();
        // Like the eye: only acts on the plugin's own lines.
        const bool enabled = NT::CaptureEnabled();
        float      value = a_own.value_or(NT::Config().duration);
        ImGuiMCP::BeginDisabled(a_hidden || !enabled);
        ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_Alpha, ImGuiMCP::GetStyle()->Alpha * (a_dim ? kDefaultTimeAlpha : 1.0F));
        // No vertical padding, like the eye (a SmallButton): a full-height
        // frame is taller than the row and overlaps the next one.
        ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, { ImGuiMCP::GetStyle()->FramePadding.x, 0.0F });
        ImGuiMCP::SetNextItemWidth(DurationSliderWidth());
        if (ImGuiMCP::SliderFloat(std::format("###time{}", a_id).c_str(), &value, NT::Limits::kDuration.min, NT::Limits::kDuration.max,
                a_own ? "%.1f s" : a_defaultLabel.c_str(), kClamp)) {
            // Whole steps, as saved: a click on "Default" (no format to round
            // by) would otherwise keep e.g. 4.2378 s for this session.
            NT::Capture::SetDuration(a_key, std::round(value / kTimeStep) * kTimeStep);
        }
        ImGuiMCP::PopStyleVar(2);
        if (ImGuiMCP::IsItemDeactivatedAfterEdit()) {
            NT::Capture::SaveDurations();
        }
        if (a_own && ImGuiMCP::IsItemClicked(ImGuiMCP::ImGuiMouseButton_Right)) {
            NT::Capture::SetDuration(a_key, std::nullopt);
            NT::Capture::SaveDurations();
        }
        ImGuiMCP::EndDisabled();
        Tooltip(!enabled ? t.historyTimeOff : t.historyTime);
    }

    // One history row without its slider: eye (or a spacer for a line that
    // cannot be hidden), time, icon glyphs, text.
    void RenderHistoryRow(const NT::Capture::HistoryEntry& a_entry, const std::string& a_key, bool a_hidden)
    {
        const auto& t = NT::Translation::S();
        if (a_key.empty()) {
            EyeSpacer(a_entry.id);
        } else {
            HideToggle(a_key, a_hidden, a_entry.id);
        }
        ImGuiMCP::SameLine();
        ImGuiMCP::TextDisabled("%s", LocalTime(a_entry.time).c_str());
        ImGuiMCP::SameLine();

        const bool noText = a_entry.text.empty();
        const auto color = a_hidden    ? *ImGuiMCP::GetStyleColorVec4(ImGuiMCP::ImGuiCol_TextDisabled) :
                           a_entry.rgb ? NT::Overlay::RgbColor(*a_entry.rgb) :
                                         *ImGuiMCP::GetStyleColorVec4(ImGuiMCP::ImGuiCol_Text);
        const int  icons = HistoryIcons(a_entry, color, !noText);
        if (noText) {
            if (icons == 0 && !a_entry.icons.empty()) {
                ImGuiMCP::TextDisabled("%s", t.historyIconOnly.c_str());  // font missing or off
            } else if (icons == 0) {
                ImGuiMCP::TextUnformatted("");  // nothing visible: end the row after the time
            }
            return;
        }
        const auto line = a_entry.count > 1 ? std::format("{} (x{})", a_entry.text, a_entry.count) : a_entry.text;
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, color);
        ImGuiMCP::TextWrapped("%s", line.c_str());
        ImGuiMCP::PopStyleColor();
    }

    // A history row plus its display time slider, which goes back up onto
    // the row's first line at a_sliderX (window-local x).
    void RenderHistoryLine(const NT::Capture::HistoryEntry& a_entry, const std::set<std::string>& a_hidden,
        const std::map<std::string, float>& a_durations, float a_sliderX, const std::string& a_defaultLabel)
    {
        const auto key = NT::Markup::LineHideKey(a_entry.text, a_entry.icons);
        const bool hidden = !key.empty() && a_hidden.contains(key);
        const auto rowStart = ImGuiMCP::GetCursorPos();
        const auto rowStartScreen = ImGuiMCP::GetCursorScreenPos();
        const float rowWidth = ImGuiMCP::GetContentRegionAvail().x;
        RenderHistoryRow(a_entry, key, hidden);
        if (key.empty()) {
            return;  // nothing to key a time on
        }
        const auto rowEnd = ImGuiMCP::GetCursorPos();
        const auto found = a_durations.find(key);
        const auto own = found != a_durations.end() ? std::optional{ found->second } : std::nullopt;
        // Hover over the whole row, from its start to the right edge of the list.
        const bool hovered = ImGuiMCP::IsMouseHoveringRect(rowStartScreen,
            { rowStartScreen.x + rowWidth, rowStartScreen.y + (rowEnd.y - rowStart.y) });
        ImGuiMCP::SetCursorPos({ a_sliderX, rowStart.y });
        DurationSlider(key, own, hidden, !own && !hovered, a_defaultLabel, a_entry.id);
        // Back down to where the row ended, through an item: a bare
        // SetCursorPos before EndChild trips ImGui's boundary check.
        ImGuiMCP::SetCursorPosY(rowEnd.y - ImGuiMCP::GetStyle()->ItemSpacing.y);
        ImGuiMCP::Dummy({ 0.0F, 0.0F });
    }

    // Page "History": the session's notifications, newest first, in their
    // own color; merged halves appear as the one line they became. Hidden
    // lines are grey with a closed eye.
    void __stdcall RenderHistory()
    {
        const auto& t = NT::Translation::S();
        static char filter[128]{};

        // A slider edit whose row vanished (history full, filter, menu
        // closed) never got its release; write it once nothing is dragged.
        if (!ImGuiMCP::IsAnyItemActive()) {
            NT::Capture::SaveDurationsIfChanged();
        }
        const auto hidden = NT::Capture::HiddenKeys();
        const auto durations = NT::Capture::Durations();

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
            // Text wraps before the slider column at the right edge.
            const float sliderX = ImGuiMCP::GetWindowContentRegionMax().x - DurationSliderWidth();
            const auto  defaultLabel = NT::Text::EscapeFormat(t.historyTimeDefault);
            ImGuiMCP::PushTextWrapPos(sliderX - ImGuiMCP::GetStyle()->ItemSpacing.x);
            for (auto it = history.rbegin(); it != history.rend(); ++it) {
                if (!needle.empty() && NT::Text::Lower(it->text).find(needle) == std::string::npos) {
                    continue;
                }
                any = true;
                RenderHistoryLine(*it, hidden, durations, sliderX, defaultLabel);
            }
            ImGuiMCP::PopTextWrapPos();
            if (!any) {
                ImGuiMCP::TextDisabled("%s", t.historyEmpty.c_str());
            }
        }
        ImGuiMCP::EndChild();
    }

    // Leaving the menu writes pending display times and ends positioning
    // even when the window was not closed through Done.
    void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_type)
    {
        if (a_type != SKSEMenuFramework::Model::kCloseMenu) {
            return;
        }
        NT::Capture::SaveDurationsIfChanged();
        if (NT::Overlay::IsPositioning() && g_positionWindow && !g_positionWindow->IsOpen) {
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
