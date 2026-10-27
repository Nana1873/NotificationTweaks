#include "Overlay.h"

#include "Capture.h"
#include "SKSEMenuFramework.h"
#include "Settings.h"
#include "Translation.h"

namespace
{
    using ImGuiMCP::ImVec2;

    std::atomic<bool> g_positioning{ false };
    bool              g_dragging = false;
    ImVec2            g_grabOffset{ 0.0F, 0.0F };

    struct Piece
    {
        std::string   text;
        std::uint32_t rgb = 0;
        bool          defaultColor = true;
        float         x = 0.0F;
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
    // a line of its own rather than being cut.
    std::vector<Line> Layout(const std::vector<NT::Markup::Run>& a_runs, int a_count, ImGuiMCP::ImFont* a_font, float a_size, float a_maxWidth)
    {
        std::vector<Line> lines(1);
        const float       space = Measure(a_font, a_size, " ");
        bool              pendingSpace = false;

        const auto place = [&](std::string_view a_word, const NT::Markup::Run* a_run) {
            const float width = Measure(a_font, a_size, a_word);
            auto*       line = &lines.back();
            const float gap = (pendingSpace && !line->pieces.empty()) ? space : 0.0F;
            if (!line->pieces.empty() && line->width + gap + width > a_maxWidth) {
                lines.emplace_back();
                line = &lines.back();
            }
            const float x = line->pieces.empty() ? 0.0F : line->width + gap;
            Piece       piece;
            piece.text = std::string(a_word);
            piece.x = x;
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

    float Alpha(const NT::Capture::Entry& a_entry, const NT::Settings& a_settings)
    {
        if (!a_entry.started) {
            return 0.0F;
        }
        const float age = a_entry.age;
        if (a_settings.fadeIn > 0.0F && age < a_settings.fadeIn) {
            return age / a_settings.fadeIn;
        }
        const float fadeStart = a_settings.fadeIn + a_settings.duration;
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
        ImGuiMCP::ImVec4 color{ a_settings.textColor[0], a_settings.textColor[1], a_settings.textColor[2], a_settings.textColor[3] * a_alpha };
        if (!a_piece.defaultColor) {
            color.x = static_cast<float>((a_piece.rgb >> 16) & 0xFF) / 255.0F;
            color.y = static_cast<float>((a_piece.rgb >> 8) & 0xFF) / 255.0F;
            color.z = static_cast<float>(a_piece.rgb & 0xFF) / 255.0F;
        }
        return ImGuiMCP::ColorConvertFloat4ToU32(color);
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
            const float lifetime = a_settings.fadeIn + a_settings.duration + a_settings.fadeOut;
            const NT::Capture::Entry* previous = nullptr;
            for (auto& entry : a_entries) {
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
                if (!it->started) {
                    continue;
                }
                if (++shown > a_settings.maxVisible) {
                    it->age = std::max(it->age, a_settings.fadeIn + a_settings.duration);
                }
            }

            std::erase_if(a_entries, [&](const NT::Capture::Entry& a_entry) { return a_entry.started && a_entry.age >= lifetime; });
            for (const auto& entry : a_entries) {
                if (entry.started) {
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
        const float o = std::max(1.0F, a_size / 18.0F);
        if (a_settings.effect == NT::TextEffect::kShadow) {
            ImGuiMCP::ImDrawListManager::AddText(a_list, a_font, a_size, { a_pos.x + o, a_pos.y + o }, shadow, begin, end);
        } else if (a_settings.effect == NT::TextEffect::kOutline) {
            for (const auto& [dx, dy] : { std::pair{ -o, 0.0F }, { o, 0.0F }, { 0.0F, -o }, { 0.0F, o }, { -o, -o }, { o, o }, { -o, o }, { o, -o } }) {
                ImGuiMCP::ImDrawListManager::AddText(a_list, a_font, a_size, { a_pos.x + dx, a_pos.y + dy }, shadow, begin, end);
            }
        }
        ImGuiMCP::ImDrawListManager::AddText(a_list, a_font, a_size, a_pos, Color(a_piece, a_settings, a_alpha), begin, end);
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
        if (!positioning && (!s.enabled || !HudVisible(ui))) {
            return;
        }

        const float dt = ui->GameIsPaused() && !positioning ? 0.0F : io->DeltaTime;
        auto        entries = Tick(dt, s);

        const auto screen = io->DisplaySize;
        auto*      font = ResolveFont();
        const float size = s.fontSize;
        const float maxWidth = std::max(50.0F, s.maxWidth * screen.x);

        struct Block
        {
            std::vector<Line> lines;
            float             alpha;
        };
        std::vector<Block> blocks;
        if (positioning && entries.empty()) {
            const auto& strings = NT::Translation::S();
            for (const auto* sample : { &strings.sample1, &strings.sample2, &strings.sample3 }) {
                blocks.push_back({ Layout(NT::Markup::Parse(*sample), 1, font, size, maxWidth), 1.0F });
            }
        } else {
            for (const auto& entry : entries) {
                blocks.push_back({ Layout(entry.runs, entry.count, font, size, maxWidth), Alpha(entry, s) });
            }
        }
        if (s.newestOnTop) {
            std::ranges::reverse(blocks);
        }

        const float lineHeight = size + s.lineSpacing;
        float       blockWidth = 0.0F;
        float       blockHeight = 0.0F;
        for (const auto& block : blocks) {
            for (const auto& line : block.lines) {
                blockWidth = std::max(blockWidth, line.width);
                blockHeight += lineHeight;
            }
        }

        const ImVec2 anchor{ s.posX * screen.x, s.posY * screen.y };
        auto*        list = ImGuiMCP::GetBackgroundDrawList();

        float y = anchor.y;
        for (const auto& block : blocks) {
            for (const auto& line : block.lines) {
                float x = anchor.x;
                if (s.align == NT::Align::kCenter) {
                    x -= line.width * 0.5F;
                } else if (s.align == NT::Align::kRight) {
                    x -= line.width;
                }
                if (s.backgroundAlpha > 0.0F && block.alpha > 0.0F) {
                    const float pad = size * 0.25F;
                    ImGuiMCP::ImDrawListManager::AddRectFilled(list, { x - pad, y - pad * 0.5F }, { x + line.width + pad, y + size + pad * 0.5F },
                        ImGuiMCP::ColorConvertFloat4ToU32({ 0.0F, 0.0F, 0.0F, s.backgroundAlpha * block.alpha }), pad, 0);
                }
                for (const auto& piece : line.pieces) {
                    DrawText(list, font, size, { x + piece.x, y }, piece, s, block.alpha);
                }
                y += lineHeight;
            }
        }

        if (!positioning) {
            g_dragging = false;
            return;
        }

        // Drag handle: the block's bounds (at least a small box at the anchor).
        const float width = std::max(blockWidth, 160.0F);
        const float height = std::max(blockHeight, lineHeight);
        float       left = anchor.x;
        if (s.align == NT::Align::kCenter) {
            left -= width * 0.5F;
        } else if (s.align == NT::Align::kRight) {
            left -= width;
        }
        const ImVec2 min{ left - 6.0F, anchor.y - 6.0F };
        const ImVec2 max{ left + width + 6.0F, anchor.y + height + 6.0F };
        const auto   mouse = ImGuiMCP::GetMousePos();
        const bool   inside = mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;
        const bool   overWindow = ImGuiMCP::IsWindowHovered(ImGuiMCP::ImGuiHoveredFlags_AnyWindow);

        if (!g_dragging && inside && !overWindow && ImGuiMCP::IsMouseClicked(0)) {
            g_dragging = true;
            g_grabOffset = { mouse.x - anchor.x, mouse.y - anchor.y };
        }
        if (g_dragging) {
            if (ImGuiMCP::IsMouseDown(0)) {
                s.posX = std::clamp((mouse.x - g_grabOffset.x) / screen.x, 0.0F, 1.0F);
                s.posY = std::clamp((mouse.y - g_grabOffset.y) / screen.y, 0.0F, 1.0F);
            } else {
                g_dragging = false;
                NT::SaveSettings();
                logs::info("Position set by dragging: {:.4f} / {:.4f}", s.posX, s.posY);
            }
        }

        const bool active = g_dragging || (inside && !overWindow);
        const auto frame = ImGuiMCP::ColorConvertFloat4ToU32({ 1.0F, 0.85F, 0.3F, active ? 1.0F : 0.6F });
        ImGuiMCP::ImDrawListManager::AddRect(list, min, max, frame, 4.0F, 0, active ? 2.5F : 1.5F);
        ImGuiMCP::ImDrawListManager::AddCircleFilled(list, anchor, 4.0F, frame, 12);
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
            g_dragging = false;
        }
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
