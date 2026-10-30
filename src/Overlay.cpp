#include "Overlay.h"

#include "Capture.h"
#include "CenterNotifications.h"
#include "IconFont.h"
#include "SKSEMenuFramework.h"
#include "Settings.h"
#include "Text.h"
#include "Translation.h"

namespace
{
    using ImGuiMCP::ImVec2;

    // Drag frame: minimum width around an empty block and padding, in pixels.
    constexpr float kDragMinWidth = 160.0F;
    constexpr float kDragPadding = 6.0F;

    std::atomic<bool> g_positioning{ false };
    ImVec2            g_grabOffset{ 0.0F, 0.0F };

    // What the mouse holds in the positioning mode.
    enum class DragTarget
    {
        kNone,
        kLines,
        kCenter
    };
    DragTarget       g_dragTarget = DragTarget::kNone;
    std::atomic<int> g_centerSelection{ 0 };  // see Overlay::SetCenterSelection

    struct Piece
    {
        std::string                                text;
        std::optional<NT::IconFont::GlyphImage>    icon;  // drawn instead of text
        std::uint32_t                              rgb = 0;
        bool                                       defaultColor = true;
        float                                      x = 0.0F;
        float                                      width = 0.0F;
    };

    struct Line
    {
        std::vector<Piece> pieces;
        float              width = 0.0F;
    };

    ImGuiMCP::ImFont* ResolveFont()
    {
        const auto& name = NT::Config().font;
        if (!name.empty()) {
            auto* io = ImGuiMCP::GetIO();
            if (io && io->Fonts) {
                const auto& fonts = io->Fonts->Fonts;
                for (int i = 0; i < fonts.Size; ++i) {
                    const char* debugName = ImGuiMCP::ImFontManger::GetDebugName(fonts.Data[i]);
                    if (debugName && name == debugName) {
                        return fonts.Data[i];
                    }
                }
            }
        }
        return ImGuiMCP::GetFont();
    }

    float Measure(ImGuiMCP::ImFont* a_font, float a_size, std::string_view a_text)
    {
        if (a_text.empty()) {
            return 0.0F;
        }
        return ImGuiMCP::ImFontManger::CalcTextSizeA(a_font, a_size, FLT_MAX, 0.0F, a_text.data(), a_text.data() + a_text.size(), nullptr).x;
    }

    // Word wrap over colored runs. A word longer than the line still gets
    // a line of its own rather than being cut. Icon-font runs become one
    // piece per glyph when a_icons is on and IconFont knows the glyph.
    std::vector<Line> Layout(const std::vector<NT::Markup::Run>& a_runs, int a_count, ImGuiMCP::ImFont* a_font, float a_size, float a_maxWidth,
        bool a_icons)
    {
        std::vector<Line> lines(1);
        const float       space = Measure(a_font, a_size, " ");
        bool              pendingSpace = false;

        const auto place = [&](std::string_view a_word, const NT::Markup::Run* a_run, std::optional<NT::IconFont::GlyphImage> a_icon = std::nullopt) {
            const float width = a_icon ? a_icon->aspect * a_size : Measure(a_font, a_size, a_word);
            auto*       line = &lines.back();
            const float gap = (pendingSpace && !line->pieces.empty()) ? space : 0.0F;
            if (!line->pieces.empty() && line->width + gap + width > a_maxWidth) {
                lines.emplace_back();
                line = &lines.back();
            }
            const float x = line->pieces.empty() ? 0.0F : line->width + gap;
            Piece       piece;
            piece.text = std::string(a_word);
            piece.icon = a_icon;
            piece.x = x;
            piece.width = width;
            if (a_run && a_run->rgb) {
                piece.rgb = *a_run->rgb;
                piece.defaultColor = false;
            }
            line->pieces.push_back(std::move(piece));
            line->width = x + width;
            pendingSpace = false;
        };

        for (const auto& run : a_runs) {
            if (run.lineBreak && !lines.back().pieces.empty()) {
                lines.emplace_back();
                pendingSpace = false;
            }
            std::size_t i = 0;
            const auto& text = run.text;
            if (!run.iconFace.empty()) {
                while (i < text.size()) {
                    const auto code = NT::Text::NextCodePoint(text, i);
                    if (code == U' ' || code == U'\t') {
                        pendingSpace = true;
                    } else if (auto icon = a_icons ? NT::IconFont::Get(run.iconFace, code, a_size) : std::nullopt) {
                        place({}, &run, icon);
                    }
                }
                continue;
            }
            while (i < text.size()) {
                if (text[i] == ' ' || text[i] == '\t') {
                    pendingSpace = true;
                    ++i;
                    continue;
                }
                const auto end = text.find_first_of(" \t", i);
                const auto word = std::string_view(text).substr(i, end == std::string::npos ? std::string::npos : end - i);
                place(word, &run);
                i += word.size();
            }
        }
        if (a_count > 1) {
            pendingSpace = true;
            place(std::format("(x{})", a_count), nullptr);
        }
        if (lines.back().pieces.empty() && lines.size() > 1) {
            lines.pop_back();
        }
        return lines;
    }

    // Fully visible time of a line: its own (History slider) or the general one.
    float Duration(const NT::Capture::Entry& a_entry, const NT::Settings& a_settings)
    {
        return a_entry.duration.value_or(a_settings.duration);
    }

    float Lifetime(const NT::Capture::Entry& a_entry, const NT::Settings& a_settings)
    {
        return a_settings.fadeIn + Duration(a_entry, a_settings) + a_settings.fadeOut;
    }

    float Alpha(const NT::Capture::Entry& a_entry, const NT::Settings& a_settings)
    {
        if (!a_entry.started) {
            return 0.0F;
        }
        const float age = a_entry.age;
        if (a_settings.fadeIn > 0.0F && age < a_settings.fadeIn) {
            return age / a_settings.fadeIn;
        }
        const float fadeStart = a_settings.fadeIn + Duration(a_entry, a_settings);
        if (age <= fadeStart) {
            return 1.0F;
        }
        if (a_settings.fadeOut <= 0.0F) {
            return 0.0F;
        }
        return std::clamp(1.0F - (age - fadeStart) / a_settings.fadeOut, 0.0F, 1.0F);
    }

    ImGuiMCP::ImU32 Color(const Piece& a_piece, const NT::Settings& a_settings, float a_alpha)
    {
        const float alpha = a_settings.textColor[3] * a_alpha;
        if (!a_piece.defaultColor) {
            return ImGuiMCP::ColorConvertFloat4ToU32(NT::Overlay::RgbColor(a_piece.rgb, alpha));
        }
        return ImGuiMCP::ColorConvertFloat4ToU32({ a_settings.textColor[0], a_settings.textColor[1], a_settings.textColor[2], alpha });
    }

    // Full-screen menus the lines would be drawn over. Checked by name, not
    // by GameIsPaused: Skyrim Souls and similar mods keep these unpaused.
    bool FullScreenMenuOpen(RE::UI* a_ui)
    {
        static constexpr std::string_view kMenus[] = { RE::InventoryMenu::MENU_NAME, RE::MagicMenu::MENU_NAME,
            RE::MapMenu::MENU_NAME, RE::StatsMenu::MENU_NAME, RE::ContainerMenu::MENU_NAME, RE::BarterMenu::MENU_NAME,
            RE::GiftMenu::MENU_NAME, RE::JournalMenu::MENU_NAME, RE::TweenMenu::MENU_NAME, RE::CraftingMenu::MENU_NAME,
            RE::LockpickingMenu::MENU_NAME, RE::BookMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME, RE::TrainingMenu::MENU_NAME,
            RE::LevelUpMenu::MENU_NAME, RE::Console::MENU_NAME, RE::RaceSexMenu::MENU_NAME };
        return std::ranges::any_of(kMenus, [&](std::string_view a_name) { return a_ui->IsMenuOpen(a_name); });
    }

    bool HudVisible(RE::UI* a_ui)
    {
        if (a_ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || a_ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
            return false;
        }
        const auto hud = a_ui->GetMenu(RE::HUDMenu::MENU_NAME);
        return hud && hud->uiMovie && hud->uiMovie->GetVisible();
    }

    // Advances ages, starts queued entries, enforces the visible limit and
    // drops finished lines. Returns the entries to draw (copied out of the lock).
    std::vector<NT::Capture::Entry> Tick(float a_dt, const NT::Settings& a_settings)
    {
        std::vector<NT::Capture::Entry> visible;
        NT::Capture::Visit([&](std::deque<NT::Capture::Entry>& a_entries) {
            const NT::Capture::Entry* previous = nullptr;
            for (auto& entry : a_entries) {
                if (entry.hidden) {
                    // Never drawn and never holds up the queue; it only
                    // stays for its lifetime so a second half can merge.
                    entry.started = true;
                    entry.age += a_dt;
                    continue;
                }
                if (!entry.started) {
                    entry.started = a_settings.immediate || !previous || !previous->started || previous->age >= a_settings.queueDelay;
                    if (!entry.started) {
                        break;  // queue mode: later entries wait as well
                    }
                } else {
                    entry.age += a_dt;
                }
                previous = &entry;
            }

            // Visible limit: the oldest lines beyond MaxVisible start fading.
            int shown = 0;
            for (auto it = a_entries.rbegin(); it != a_entries.rend(); ++it) {
                if (!it->started || it->hidden) {
                    continue;
                }
                if (++shown > a_settings.maxVisible) {
                    it->age = std::max(it->age, a_settings.fadeIn + Duration(*it, a_settings));
                }
            }

            std::erase_if(a_entries, [&](const NT::Capture::Entry& a_entry) {
                return a_entry.started && a_entry.age >= Lifetime(a_entry, a_settings);
            });
            for (const auto& entry : a_entries) {
                if (entry.started && !entry.hidden) {
                    visible.push_back(entry);
                }
            }
        });
        return visible;
    }

    void DrawText(ImGuiMCP::ImDrawList* a_list, ImGuiMCP::ImFont* a_font, float a_size, ImVec2 a_pos, const Piece& a_piece,
        const NT::Settings& a_settings, float a_alpha)
    {
        const char* begin = a_piece.text.c_str();
        const char* end = begin + a_piece.text.size();
        const auto  shadow = ImGuiMCP::ColorConvertFloat4ToU32({ 0.0F, 0.0F, 0.0F, 0.85F * a_alpha * a_settings.textColor[3] });
        // Whole pixels: ImGui truncates each text start, so a fractional
        // offset came out 1 or 2 px depending on the line's y (shadow visible
        // on some lines, hidden under the text on others).
        const float o = std::max(1.0F, std::round(a_size / 18.0F));
        // Icons: the white glyph image tinted like text, its box one line high.
        const auto draw = [&](float a_dx, float a_dy, ImGuiMCP::ImU32 a_color) {
            const ImVec2 pos{ a_pos.x + a_dx, a_pos.y + a_dy };
            if (a_piece.icon) {
                ImGuiMCP::ImDrawListManager::AddImage(a_list, a_piece.icon->texture, pos, { pos.x + a_piece.width, pos.y + a_size },
                    a_piece.icon->uv0, a_piece.icon->uv1, a_color);
            } else {
                ImGuiMCP::ImDrawListManager::AddText(a_list, a_font, a_size, pos, a_color, begin, end);
            }
        };
        if (a_settings.effect == NT::TextEffect::kShadow) {
            draw(o, o, shadow);
        } else if (a_settings.effect == NT::TextEffect::kOutline) {
            for (const auto& [dx, dy] : { std::pair{ -o, 0.0F }, { o, 0.0F }, { 0.0F, -o }, { 0.0F, o }, { -o, -o }, { o, o }, { -o, o }, { o, -o } }) {
                draw(dx, dy, shadow);
            }
        }
        draw(0.0F, 0.0F, Color(a_piece, a_settings, a_alpha));
    }

    struct Block
    {
        std::vector<Line> lines;
        float             alpha;
    };

    // Text size and spacing for the current screen, scaled from 1080p.
    struct Metrics
    {
        ImGuiMCP::ImFont* font;
        float             size;
        float             lineHeight;
        float             maxWidth;
    };

    // Horizontal shift that turns the anchor into the left edge of a line
    // (or block) a_width wide.
    float AlignOffset(NT::Align a_align, float a_width)
    {
        switch (a_align) {
        case NT::Align::kCenter:
            return -a_width * 0.5F;
        case NT::Align::kRight:
            return -a_width;
        default:
            return 0.0F;
        }
    }

    // Lays out the entries (or the sample lines while positioning with
    // nothing to show) in drawing order.
    std::vector<Block> BuildBlocks(const std::vector<NT::Capture::Entry>& a_entries, bool a_positioning, const Metrics& a_metrics,
        const NT::Settings& a_settings)
    {
        std::vector<Block> blocks;
        if (a_positioning && a_entries.empty()) {
            const auto& strings = NT::Translation::S();
            for (const auto* sample : { &strings.sample1, &strings.sample2, &strings.sample3 }) {
                blocks.push_back({ Layout(NT::Markup::Parse(*sample), 1, a_metrics.font, a_metrics.size, a_metrics.maxWidth, false), 1.0F });
            }
        } else {
            for (const auto& entry : a_entries) {
                blocks.push_back({ Layout(entry.runs, entry.count, a_metrics.font, a_metrics.size, a_metrics.maxWidth, a_settings.drawIconFonts), Alpha(entry, a_settings) });
            }
        }
        if (a_settings.newestOnTop) {
            std::ranges::reverse(blocks);
        }
        return blocks;
    }

    void DrawBlocks(ImGuiMCP::ImDrawList* a_list, const std::vector<Block>& a_blocks, ImVec2 a_anchor, const Metrics& a_metrics,
        const NT::Settings& a_settings)
    {
        float y = a_anchor.y;
        for (const auto& block : a_blocks) {
            for (const auto& line : block.lines) {
                const float x = a_anchor.x + AlignOffset(a_settings.align, line.width);
                if (a_settings.backgroundAlpha > 0.0F && block.alpha > 0.0F) {
                    const float pad = a_metrics.size * 0.25F;
                    ImGuiMCP::ImDrawListManager::AddRectFilled(a_list, { x - pad, y - pad * 0.5F },
                        { x + line.width + pad, y + a_metrics.size + pad * 0.5F },
                        ImGuiMCP::ColorConvertFloat4ToU32({ 0.0F, 0.0F, 0.0F, a_settings.backgroundAlpha * block.alpha }), pad, 0);
                }
                for (const auto& piece : line.pieces) {
                    DrawText(a_list, a_metrics.font, a_metrics.size, { x + piece.x, y }, piece, a_settings, block.alpha);
                }
                y += a_metrics.lineHeight;
            }
        }
    }

    // Positioning mode: frame around the block; a left-button drag inside it
    // moves the anchor and saves on release.
    void HandleDrag(ImGuiMCP::ImDrawList* a_list, const std::vector<Block>& a_blocks, ImVec2 a_anchor, ImVec2 a_screen,
        const Metrics& a_metrics, NT::Settings& a_settings)
    {
        float blockWidth = 0.0F;
        float blockHeight = 0.0F;
        for (const auto& block : a_blocks) {
            for (const auto& line : block.lines) {
                blockWidth = std::max(blockWidth, line.width);
                blockHeight += a_metrics.lineHeight;
            }
        }

        // Drag handle: the block's bounds (at least a small box at the anchor).
        const float  width = std::max(blockWidth, kDragMinWidth);
        const float  height = std::max(blockHeight, a_metrics.lineHeight);
        const float  left = a_anchor.x + AlignOffset(a_settings.align, width);
        const ImVec2 min{ left - kDragPadding, a_anchor.y - kDragPadding };
        const ImVec2 max{ left + width + kDragPadding, a_anchor.y + height + kDragPadding };
        const auto   mouse = ImGuiMCP::GetMousePos();
        const bool   inside = mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
        const bool   overWindow = ImGuiMCP::IsWindowHovered(ImGuiMCP::ImGuiHoveredFlags_AnyWindow);

        if (g_dragTarget == DragTarget::kNone && inside && !overWindow && ImGuiMCP::IsMouseClicked(0)) {
            g_dragTarget = DragTarget::kLines;
            g_grabOffset = { mouse.x - a_anchor.x, mouse.y - a_anchor.y };
        }
        const bool dragging = g_dragTarget == DragTarget::kLines;
        if (dragging) {
            if (ImGuiMCP::IsMouseDown(0)) {
                a_settings.posX = std::clamp((mouse.x - g_grabOffset.x) / a_screen.x, 0.0F, 1.0F);
                a_settings.posY = std::clamp((mouse.y - g_grabOffset.y) / a_screen.y, 0.0F, 1.0F);
            } else {
                g_dragTarget = DragTarget::kNone;
                NT::SaveSettings();
                logs::info("Position set by dragging: {:.4f} / {:.4f}", a_settings.posX, a_settings.posY);
            }
        }

        const bool active = dragging || (inside && !overWindow && g_dragTarget == DragTarget::kNone);
        const auto frame = ImGuiMCP::ColorConvertFloat4ToU32({ 1.0F, 0.85F, 0.3F, active ? 1.0F : 0.6F });
        ImGuiMCP::ImDrawListManager::AddRect(a_list, min, max, frame, 4.0F, 0, active ? 2.5F : 1.5F);
        ImGuiMCP::ImDrawListManager::AddCircleFilled(a_list, a_anchor, 4.0F, frame, 12);
    }

    // Screen rect of a center group's text area at its placement.
    std::pair<ImVec2, ImVec2> CenterFrame(const NT::CenterNotifications::Geometry& a_geometry, const NT::CenterPlacement& a_place)
    {
        using namespace NT::CenterNotifications;
        const float unit = a_geometry.pxPerUnit;
        const float cx = a_geometry.originX + a_place.x * unit;
        const float cy = a_geometry.originY + (a_place.y + kTextOffsetY * a_place.scale) * unit;
        const float halfW = kFrameWidth * 0.5F * a_place.scale * unit;
        const float halfH = kFrameHeight * 0.5F * a_place.scale * unit;
        return { { cx - halfW, cy - halfH }, { cx + halfW, cy + halfH } };
    }

    // Positioning mode: a labelled frame per center group (where its title
    // line shows). The selected group, or all of them together, follows a
    // left-button drag; saved on release.
    void HandleCenterDrag(ImGuiMCP::ImDrawList* a_list, NT::Settings& a_settings)
    {
        const auto geometry = NT::CenterNotifications::GetGeometry();
        if (!geometry.valid) {
            return;
        }
        const auto&       t = NT::Translation::S();
        const std::string* names[NT::kCenterGroups] = { &t.centerLocation, &t.centerQuest, &t.centerSkill, &t.centerShout };
        const int         selection = g_centerSelection.load();
        const auto        selected = [&](std::size_t a_group) { return selection == 0 || selection == static_cast<int>(a_group) + 1; };
        const auto        mouse = ImGuiMCP::GetMousePos();
        const bool        overWindow = ImGuiMCP::IsWindowHovered(ImGuiMCP::ImGuiHoveredFlags_AnyWindow);

        bool hovered = false;
        for (std::size_t i = 0; i < NT::kCenterGroups; ++i) {
            const auto [min, max] = CenterFrame(geometry, a_settings.center[i]);
            if (selected(i) && mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y) {
                hovered = true;
            }
        }
        if (g_dragTarget == DragTarget::kNone && hovered && !overWindow && ImGuiMCP::IsMouseClicked(0)) {
            g_dragTarget = DragTarget::kCenter;
            g_grabOffset = mouse;
        }
        const bool dragging = g_dragTarget == DragTarget::kCenter;
        if (dragging) {
            if (ImGuiMCP::IsMouseDown(0)) {
                // Move every selected group by the mouse delta, in HUD units.
                const float dx = (mouse.x - g_grabOffset.x) / geometry.pxPerUnit;
                const float dy = (mouse.y - g_grabOffset.y) / geometry.pxPerUnit;
                g_grabOffset = mouse;
                for (std::size_t i = 0; i < NT::kCenterGroups; ++i) {
                    if (selected(i)) {
                        auto& place = a_settings.center[i];
                        place.x = std::clamp(place.x + dx, NT::Limits::kCenterX.min, NT::Limits::kCenterX.max);
                        place.y = std::clamp(place.y + dy, NT::Limits::kCenterY.min, NT::Limits::kCenterY.max);
                    }
                }
            } else {
                g_dragTarget = DragTarget::kNone;
                NT::SaveSettings();
                logs::info("Center notifications moved by dragging");
            }
        }

        // Unselected groups dimmed; labels stacked so frames on the same spot
        // (the default) stay readable.
        const float lineHeight = ImGuiMCP::GetTextLineHeight();
        for (std::size_t i = 0; i < NT::kCenterGroups; ++i) {
            const auto [min, max] = CenterFrame(geometry, a_settings.center[i]);
            const bool  active = selected(i) && (dragging || (hovered && !overWindow && g_dragTarget == DragTarget::kNone));
            const float alpha = !selected(i) ? 0.3F : active ? 1.0F : 0.6F;
            const auto  color = ImGuiMCP::ColorConvertFloat4ToU32({ 0.45F, 0.8F, 1.0F, alpha });
            ImGuiMCP::ImDrawListManager::AddRect(a_list, min, max, color, 4.0F, 0, active ? 2.5F : 1.5F);
            const ImVec2 label{ min.x + 6.0F, min.y + 4.0F + static_cast<float>(i) * lineHeight };
            ImGuiMCP::ImDrawListManager::AddText(a_list, label, color, names[i]->c_str(), nullptr);
        }
    }

    void __stdcall Render()
    {
        auto* ui = RE::UI::GetSingleton();
        auto* io = ImGuiMCP::GetIO();
        if (!ui || !io) {
            return;
        }
        auto&      s = NT::Config();
        const bool positioning = g_positioning.load();
        if (!s.enabled && !positioning) {
            NT::Capture::ClearActive();  // no stale lines when it is switched back on
            return;
        }
        if (!positioning && !HudVisible(ui)) {
            return;
        }
        // While the framework's own menu is open the lines stay visible as a
        // preview for the settings.
        if (!positioning && !s.showInMenus && FullScreenMenuOpen(ui) && !SKSEMenuFramework::IsAnyBlockingWindowOpened()) {
            return;  // lines wait (and do not age) until the menu closes
        }

        const float dt = ui->GameIsPaused() && !positioning ? 0.0F : io->DeltaTime;
        const auto  entries = Tick(dt, s);

        const auto  screen = io->DisplaySize;
        const float scale = screen.y > 0.0F ? screen.y / NT::kReferenceHeight : 1.0F;
        Metrics     metrics;
        metrics.font = ResolveFont();
        metrics.size = s.fontSize * scale;
        metrics.lineHeight = metrics.size + s.lineSpacing * scale;
        metrics.maxWidth = std::max(50.0F, s.maxWidth * screen.x);

        const auto   blocks = BuildBlocks(entries, positioning, metrics, s);
        const ImVec2 anchor{ s.posX * screen.x, s.posY * screen.y };
        auto*        list = ImGuiMCP::GetBackgroundDrawList();
        DrawBlocks(list, blocks, anchor, metrics, s);

        if (positioning) {
            HandleDrag(list, blocks, anchor, screen, metrics, s);
            HandleCenterDrag(list, s);
        } else {
            g_dragTarget = DragTarget::kNone;
        }
    }
}

namespace NT::Overlay
{
    void Initialize()
    {
        SKSEMenuFramework::AddHudElement(Render);
        logs::info("Registered HUD element");
    }

    void SetPositioning(bool a_on)
    {
        g_positioning = a_on;
        if (!a_on) {
            g_dragTarget = DragTarget::kNone;
        }
    }

    void SetCenterSelection(int a_selection)
    {
        g_centerSelection = std::clamp(a_selection, 0, static_cast<int>(NT::kCenterGroups));
    }

    int CenterSelection()
    {
        return g_centerSelection.load();
    }

    bool IsPositioning()
    {
        return g_positioning.load();
    }

    std::vector<std::string> FontNames()
    {
        std::vector<std::string> names;
        auto*                    io = ImGuiMCP::GetIO();
        if (!io || !io->Fonts) {
            return names;
        }
        const auto& fonts = io->Fonts->Fonts;
        for (int i = 0; i < fonts.Size; ++i) {
            const char* name = ImGuiMCP::ImFontManger::GetDebugName(fonts.Data[i]);
            if (name && *name && std::ranges::find(names, name) == names.end()) {
                names.emplace_back(name);
            }
        }
        return names;
    }
}
