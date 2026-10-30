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

    // Layout editor: the lines' frame (minimum width around an empty block,
    // padding), the center boxes' look and the wheel steps.
    constexpr float           kDragMinWidth = 160.0F;
    constexpr float           kDragPadding = 6.0F;
    constexpr ImGuiMCP::ImVec4 kLinesFrameRgb{ 1.0F, 0.85F, 0.3F, 1.0F };
    constexpr ImGuiMCP::ImVec4 kCenterBoxRgb{ 0.45F, 0.8F, 1.0F, 1.0F };
    constexpr ImGuiMCP::ImVec4 kCenterFillRgb{ 0.05F, 0.08F, 0.12F, 1.0F };
    constexpr float           kCenterLabelPad = 4.0F;
    constexpr float           kCenterScaleStep = 0.05F;  // per wheel notch
    constexpr float           kFontSizeStep = 1.0F;      // px at 1080p per wheel notch
    constexpr double          kWheelSaveDelay = 0.5;     // seconds after the last notch or arrow key
    constexpr float           kArrowStep = 1.0F;         // screen pixels per arrow key press (Shift: x10)

    std::atomic<bool> g_positioning{ false };

    // What the mouse holds in the layout editor, and where it took it.
    enum class Target
    {
        kNone,
        kLines,
        kCenter
    };
    Target                 g_dragTarget = Target::kNone;
    std::size_t            g_dragGroup = 0;  // the center group held with kCenter
    Target                 g_selected = Target::kLines;  // last clicked frame, moved by the arrow keys
    std::size_t            g_selectedGroup = 0;
    ImVec2                 g_grabMouse{ 0.0F, 0.0F };
    ImVec2                 g_grabLines{ 0.0F, 0.0F };  // left edge and top of the lines' block at grab, screen pixels
    NT::CenterPlacement    g_grabPlace;                 // the group's placement at grab
    double                 g_saveAt = 0.0;              // pending save (wheel), ImGui time; 0 = none

    ImGuiMCP::ImU32 Rgba(const ImGuiMCP::ImVec4& a_rgb, float a_alpha)
    {
        return ImGuiMCP::ColorConvertFloat4ToU32({ a_rgb.x, a_rgb.y, a_rgb.z, a_alpha });
    }

    bool Contains(const std::pair<ImVec2, ImVec2>& a_rect, ImVec2 a_point)
    {
        return a_point.x >= a_rect.first.x && a_point.x <= a_rect.second.x && a_point.y >= a_rect.first.y && a_point.y <= a_rect.second.y;
    }

    const std::string& CenterName(std::size_t a_group)
    {
        const auto& t = NT::Translation::S();
        const std::string* names[NT::kCenterGroups] = { &t.centerLocation, &t.centerQuest, &t.centerSkill, &t.centerShout };
        return *names[a_group];
    }

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

    // A line in another alphabet that no loaded font has (SMF loads only the
    // Character Glyphs chosen in its settings); shown as a hint in the menu.
    // Cleared once such a line finds a font again.
    std::atomic<bool> g_missingGlyphs{ false };

    // True when a_font has a glyph for every letter of a_runs. ASCII, icon
    // runs, general punctuation (… and the like), code points beyond the
    // 16-bit ImWchar and broken UTF-8 are not checked: no alphabet font
    // changes how they show.
    bool HasGlyphs(ImGuiMCP::ImFont* a_font, const std::vector<NT::Markup::Run>& a_runs)
    {
        using FindGlyph = const ImGuiMCP::ImFontGlyph* (*)(ImGuiMCP::ImFont*, ImGuiMCP::ImWchar);
        static const auto find = GetMenuFrameworkFunction<FindGlyph>("ImFont_FindGlyphNoFallback");
        if (!find || !a_font) {
            return true;
        }
        for (const auto& run : a_runs) {
            if (!run.iconFace.empty()) {
                continue;
            }
            for (std::size_t i = 0; i < run.text.size();) {
                const auto code = NT::Text::NextCodePoint(run.text, i);
                if (code < 0x80 || code > 0xFFFF || (code >= 0x2000 && code <= 0x206F) || code == U'�') {
                    continue;
                }
                if (!find(a_font, static_cast<ImGuiMCP::ImWchar>(code))) {
                    return false;
                }
            }
        }
        return true;
    }

    // The chosen font, or for a line it lacks letters of the first atlas
    // font that has them all (SMF's language fonts: RU, PL, CN, ...). Only
    // that line switches; a line no font fits stays in the chosen one.
    ImGuiMCP::ImFont* FontFor(const std::vector<NT::Markup::Run>& a_runs, ImGuiMCP::ImFont* a_chosen)
    {
        if (HasGlyphs(a_chosen, a_runs)) {
            return a_chosen;
        }
        if (auto* io = ImGuiMCP::GetIO(); io && io->Fonts) {
            const auto& fonts = io->Fonts->Fonts;
            for (int i = 0; i < fonts.Size; ++i) {
                if (fonts.Data[i] != a_chosen && HasGlyphs(fonts.Data[i], a_runs)) {
                    g_missingGlyphs = false;
                    return fonts.Data[i];
                }
            }
        }
        g_missingGlyphs = true;
        return a_chosen;
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
        ImGuiMCP::ImFont* font;  // the chosen one or FontFor's fallback
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
            for (const auto* sample : { &strings.sample1, &strings.sample2 }) {
                const auto  runs = NT::Markup::Parse(*sample);
                auto* const font = FontFor(runs, a_metrics.font);
                blocks.push_back({ Layout(runs, 1, font, a_metrics.size, a_metrics.maxWidth, false), 1.0F, font });
            }
        } else {
            for (const auto& entry : a_entries) {
                auto* const font = FontFor(entry.runs, a_metrics.font);
                blocks.push_back({ Layout(entry.runs, entry.count, font, a_metrics.size, a_metrics.maxWidth, a_settings.drawIconFonts), Alpha(entry, a_settings), font });
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
                    DrawText(a_list, block.font, a_metrics.size, { x + piece.x, y }, piece, a_settings, block.alpha);
                }
                y += a_metrics.lineHeight;
            }
        }
    }

    // Layout editor: the lines' frame and a labelled box per center group
    // (where its title line shows). The topmost frame under the mouse takes
    // the gesture: center boxes above the lines, Locations on top (the group
    // most players move), so dragging one off the pile brings the next up.
    // Left drag moves (saved on release), the wheel resizes (lines: font
    // size; boxes: scale; saved shortly after), right-click puts it back.
    // Places the lines' block with its left edge at a_left and its top at
    // a_top (screen pixels). The alignment follows the block's center: left
    // third left-aligned, right third right-aligned (lines grow away from
    // the screen edge), else centered; posX is the anchor of that alignment.
    void PlaceLines(NT::Settings& a_settings, float a_left, float a_top, float a_width, ImVec2 a_screen)
    {
        const float center = a_left + a_width * 0.5F;
        a_settings.align = center < a_screen.x / 3.0F ? NT::Align::kLeft : center > a_screen.x * 2.0F / 3.0F ? NT::Align::kRight : NT::Align::kCenter;
        a_settings.posX = std::clamp((a_left - AlignOffset(a_settings.align, a_width)) / a_screen.x, 0.0F, 1.0F);
        a_settings.posY = std::clamp(a_top / a_screen.y, 0.0F, 1.0F);
    }

    void HandleLayoutEditor(ImGuiMCP::ImDrawList* a_list, const std::vector<Block>& a_blocks, ImVec2 a_anchor, ImVec2 a_screen,
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
        // Lines handle: the block's bounds (at least a small box at the anchor).
        const float width = std::max(blockWidth, kDragMinWidth);
        const float height = std::max(blockHeight, a_metrics.lineHeight);
        const float left = a_anchor.x + AlignOffset(a_settings.align, width);
        const std::pair<ImVec2, ImVec2> lines{ { left - kDragPadding, a_anchor.y - kDragPadding },
            { left + width + kDragPadding, a_anchor.y + height + kDragPadding } };

        const auto geometry = NT::CenterNotifications::GetGeometry();
        std::array<std::pair<ImVec2, ImVec2>, NT::kCenterGroups> boxes{};
        for (std::size_t i = 0; geometry.valid && i < NT::kCenterGroups; ++i) {
            const auto r = geometry.TitleRect(a_settings.center[i]);
            boxes[i] = { { r[0], r[1] }, { r[2], r[3] } };
        }

        // The frame under the mouse: center boxes in group order (0 on top),
        // then the lines.
        const auto mouse = ImGuiMCP::GetMousePos();
        Target      hovered = Target::kNone;
        std::size_t hoveredGroup = 0;
        if (!ImGuiMCP::IsWindowHovered(ImGuiMCP::ImGuiHoveredFlags_AnyWindow)) {
            for (std::size_t i = 0; geometry.valid && i < NT::kCenterGroups && hovered == Target::kNone; ++i) {
                if (Contains(boxes[i], mouse)) {
                    hovered = Target::kCenter;
                    hoveredGroup = i;
                }
            }
            if (hovered == Target::kNone && Contains(lines, mouse)) {
                hovered = Target::kLines;
            }
        }

        if (g_dragTarget == Target::kNone && hovered != Target::kNone) {
            auto* place = hovered == Target::kCenter ? &a_settings.center[hoveredGroup] : nullptr;
            if (ImGuiMCP::IsMouseClicked(0)) {
                g_dragTarget = hovered;
                g_dragGroup = hoveredGroup;
                g_selected = hovered;
                g_selectedGroup = hoveredGroup;
                g_grabMouse = mouse;
                g_grabLines = { left, a_anchor.y };
                g_grabPlace = place ? *place : NT::CenterPlacement{};
            } else if (ImGuiMCP::IsMouseClicked(1)) {
                if (place) {
                    *place = {};  // back to the HUD's own spot and size (Vanilla or a UI mod)
                } else {
                    const NT::Settings defaults;
                    a_settings.posX = defaults.posX;
                    a_settings.posY = defaults.posY;
                    a_settings.align = defaults.align;
                }
                NT::SaveSettings();
            } else if (const float wheel = ImGuiMCP::GetIO()->MouseWheel; wheel != 0.0F) {
                if (place) {
                    place->scale = std::clamp(place->scale + wheel * kCenterScaleStep, NT::Limits::kCenterScale.min, NT::Limits::kCenterScale.max);
                } else {
                    a_settings.fontSize = std::clamp(a_settings.fontSize + wheel * kFontSizeStep, NT::Limits::kFontSize.min, NT::Limits::kFontSize.max);
                }
                g_saveAt = ImGuiMCP::GetTime() + kWheelSaveDelay;  // one file write per scroll, not per notch
            }
        }

        if (g_dragTarget != Target::kNone) {
            const ImVec2 delta{ mouse.x - g_grabMouse.x, mouse.y - g_grabMouse.y };
            if (ImGuiMCP::IsMouseDown(0)) {
                if (g_dragTarget == Target::kLines) {
                    PlaceLines(a_settings, g_grabLines.x + delta.x, g_grabLines.y + delta.y, width, a_screen);
                } else if (geometry.valid) {
                    auto& place = a_settings.center[g_dragGroup];
                    place.x = std::clamp(g_grabPlace.x + delta.x / geometry.pxPerUnitX, NT::Limits::kCenterX.min, NT::Limits::kCenterX.max);
                    place.y = std::clamp(g_grabPlace.y + delta.y / geometry.pxPerUnitY, NT::Limits::kCenterY.min, NT::Limits::kCenterY.max);
                }
            } else {
                logs::info("Layout editor: {} moved", g_dragTarget == Target::kLines ? "lines" : NT::kCenterKeys[g_dragGroup]);
                g_dragTarget = Target::kNone;
                NT::SaveSettings();
            }
        }
        // Arrow keys: fine moves of the selected frame, one pixel (Shift: ten).
        if (g_dragTarget == Target::kNone) {
            const float step = kArrowStep * (ImGuiMCP::IsKeyDown(ImGuiMCP::ImGuiMod_Shift) ? 10.0F : 1.0F);
            const ImVec2 move{ (ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_RightArrow) ? step : 0.0F) -
                                   (ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_LeftArrow) ? step : 0.0F),
                (ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_DownArrow) ? step : 0.0F) -
                    (ImGuiMCP::IsKeyPressed(ImGuiMCP::ImGuiKey_UpArrow) ? step : 0.0F) };
            if (move.x != 0.0F || move.y != 0.0F) {
                if (g_selected == Target::kLines) {
                    PlaceLines(a_settings, left + move.x, a_anchor.y + move.y, width, a_screen);
                } else if (geometry.valid) {
                    auto& place = a_settings.center[g_selectedGroup];
                    place.x = std::clamp(place.x + move.x / geometry.pxPerUnitX, NT::Limits::kCenterX.min, NT::Limits::kCenterX.max);
                    place.y = std::clamp(place.y + move.y / geometry.pxPerUnitY, NT::Limits::kCenterY.min, NT::Limits::kCenterY.max);
                }
                g_saveAt = ImGuiMCP::GetTime() + kWheelSaveDelay;
            }
        }
        if (g_saveAt > 0.0 && ImGuiMCP::GetTime() >= g_saveAt) {
            g_saveAt = 0.0;
            NT::SaveSettings();
        }

        // Held or hovered (or, with nothing under the mouse, the selection
        // the arrow keys move) is drawn bright.
        const auto active = [&](Target a_target, std::size_t a_group) {
            const Target held = g_dragTarget != Target::kNone ? g_dragTarget : hovered != Target::kNone ? hovered : g_selected;
            const std::size_t group = g_dragTarget != Target::kNone ? g_dragGroup : hovered != Target::kNone ? hoveredGroup : g_selectedGroup;
            return held == a_target && (a_target != Target::kCenter || group == a_group);
        };

        const bool linesActive = active(Target::kLines, 0);
        ImGuiMCP::ImDrawListManager::AddRect(a_list, lines.first, lines.second, Rgba(kLinesFrameRgb, linesActive ? 1.0F : 0.6F), 4.0F, 0,
            linesActive ? 2.5F : 1.5F);
        ImGuiMCP::ImDrawListManager::AddCircleFilled(a_list, a_anchor, 4.0F, Rgba(kLinesFrameRgb, linesActive ? 1.0F : 0.6F), 12);

        // Drawn bottom to top. A dark title bar hides the labels of the boxes
        // below, so only the top one reads; the inside stays see-through.
        const float barHeight = ImGuiMCP::GetTextLineHeight() + 2.0F * kCenterLabelPad;
        for (std::size_t i = NT::kCenterGroups; geometry.valid && i-- > 0;) {
            const auto& [min, max] = boxes[i];
            const bool  on = active(Target::kCenter, i);
            const auto  color = Rgba(kCenterBoxRgb, on ? 1.0F : 0.7F);
            ImGuiMCP::ImDrawListManager::AddRectFilled(a_list, min, max, Rgba(kCenterFillRgb, 0.2F), 4.0F, 0);
            ImGuiMCP::ImDrawListManager::AddRectFilled(a_list, min, { max.x, std::min(max.y, min.y + barHeight) }, Rgba(kCenterFillRgb, 0.9F), 4.0F, 0);
            ImGuiMCP::ImDrawListManager::AddRect(a_list, min, max, color, 4.0F, 0, on ? 2.5F : 1.5F);
            ImGuiMCP::ImDrawListManager::AddText(a_list, { min.x + 1.5F * kCenterLabelPad, min.y + kCenterLabelPad }, color, CenterName(i).c_str(), nullptr);
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
            HandleLayoutEditor(list, blocks, anchor, screen, metrics, s);
        } else {
            g_dragTarget = Target::kNone;
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
            g_dragTarget = Target::kNone;
            g_saveAt = 0.0;  // the caller saves when the editor closes
        }
    }


    bool IsPositioning()
    {
        return g_positioning.load();
    }

    bool MissingGlyphs()
    {
        return g_missingGlyphs.load();
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
