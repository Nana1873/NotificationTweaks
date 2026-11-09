#include "IconFont.h"

#include "Markup.h"
#include "SwfFont.h"
#include "Text.h"

#include <RE/B/BSResourceNiBinaryStream.h>

namespace
{
    // Each glyph is rasterized at these heights (ascent + descent) and the
    // overlay samples the one closest above its line height: the menu
    // framework's sampler has no mipmaps, so a single large copy would alias
    // when shrunk.
    constexpr int kLevels[] = { 96, 48, 24 };
    constexpr int kPadding = 2;
    constexpr int kAtlasWidth = 1024;
    constexpr int kMaxAtlasHeight = 8192;

    struct AtlasGlyph
    {
        int   x[std::size(kLevels)]{};
        int   y[std::size(kLevels)]{};
        int   width[std::size(kLevels)]{};
        float aspect = 1.0F;
    };

    struct LoadedFont
    {
        std::string                              face;  // "$Iconographia"
        std::unordered_map<char32_t, AtlasGlyph> glyphs;
        std::vector<std::uint8_t>                rgba;  // atlas pixels until the texture exists
        int                                      atlasHeight = 0;
        REX::W32::ID3D11ShaderResourceView*      view = nullptr;
        bool                                     textureFailed = false;
    };

    std::mutex                                                   g_lock;
    bool                                                         g_configRead = false;
    NT::SwfFont::FontConfig                                      g_config;
    std::unordered_map<std::string, std::unique_ptr<LoadedFont>> g_fonts;  // lowercase face -> font, null = not available

    std::vector<std::uint8_t> ReadFile(const std::string& a_path)
    {
        std::vector<std::uint8_t>    data;
        RE::BSResourceNiBinaryStream stream{ a_path };
        if (!stream.good()) {
            return data;
        }
        std::vector<std::uint8_t> chunk(64 * 1024);
        std::uint64_t             read = 0;
        do {
            read = 0;
            stream.stream->DoRead(chunk.data(), chunk.size(), read);
            data.insert(data.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(read));
        } while (read == chunk.size());
        return data;
    }

    // Caller holds g_lock.
    const NT::SwfFont::FontConfig& Config()
    {
        if (!g_configRead) {
            g_configRead = true;
            const auto text = ReadFile("Interface\\fontconfig.txt");
            g_config = NT::SwfFont::ParseFontConfig({ reinterpret_cast<const char*>(text.data()), text.size() });
            logs::info("fontconfig.txt: {} font librar(ies), {} map(s)", g_config.libs.size(), g_config.maps.size());
        }
        return g_config;
    }

    // Packs every glyph at every level into one atlas: white RGB everywhere
    // (no dark fringe when the sampler reaches into the padding), coverage
    // as alpha. Glyphs that would push the atlas past kMaxAtlasHeight are
    // left out.
    std::unique_ptr<LoadedFont> BuildAtlas(std::string_view a_face, const NT::SwfFont::Font& a_font)
    {
        auto loaded = std::make_unique<LoadedFont>();
        loaded->face = std::string(a_face);
        const float emHeight = a_font.ascent + a_font.descent;

        struct Box
        {
            const NT::SwfFont::Glyph* glyph;
            float                     left;   // EM: min(0, outline left)
            float                     right;  // EM: max(advance, outline right)
        };
        std::vector<Box> boxes;
        for (const auto& glyph : a_font.glyphs) {
            if (glyph.contours.empty() || glyph.advance <= 0.0F) {
                continue;
            }
            Box box{ &glyph, 0.0F, glyph.advance };
            for (const auto& contour : glyph.contours) {
                for (const auto& point : contour) {
                    box.left = std::min(box.left, point.x);
                    box.right = std::max(box.right, point.x);
                }
            }
            boxes.push_back(box);
        }

        // Layout pass: rows per level, one level after the other.
        std::vector<AtlasGlyph> placed(boxes.size());
        std::size_t             fitting = boxes.size();
        int                     y = 0;
        for (std::size_t level = 0; level < std::size(kLevels); ++level) {
            const int   height = kLevels[level];
            const float scale = height / emHeight;
            int         x = 0;
            for (std::size_t i = 0; i < fitting; ++i) {
                const int width = std::clamp(static_cast<int>(std::ceil((boxes[i].right - boxes[i].left) * scale)), 1, kAtlasWidth - 2 * kPadding);
                if (x + width + 2 * kPadding > kAtlasWidth) {
                    x = 0;
                    y += height + 2 * kPadding;
                }
                if (y + height + 2 * kPadding > kMaxAtlasHeight) {
                    fitting = i;  // later glyphs get no slot at any level
                    break;
                }
                placed[i].x[level] = x + kPadding;
                placed[i].y[level] = y + kPadding;
                placed[i].width[level] = width;
                placed[i].aspect = (boxes[i].right - boxes[i].left) / emHeight;
                x += width + 2 * kPadding;
            }
            y += height + 2 * kPadding;
        }
        if (fitting < boxes.size()) {
            logs::warn("Icon font {}: atlas full, {} of {} glyph(s) kept", a_face, fitting, boxes.size());
        }
        loaded->atlasHeight = std::min(y, kMaxAtlasHeight);

        // Raster pass.
        loaded->rgba.assign(static_cast<std::size_t>(kAtlasWidth) * static_cast<std::size_t>(loaded->atlasHeight) * 4, 255);
        for (std::size_t p = 3; p < loaded->rgba.size(); p += 4) {
            loaded->rgba[p] = 0;
        }
        for (std::size_t i = 0; i < fitting; ++i) {
            for (std::size_t level = 0; level < std::size(kLevels); ++level) {
                const int   height = kLevels[level];
                const float scale = height / emHeight;
                const int   width = placed[i].width[level];
                const auto  coverage =
                    NT::SwfFont::Rasterize(boxes[i].glyph->contours, width, height, scale, -boxes[i].left * scale, a_font.ascent * scale);
                for (int row = 0; row < height; ++row) {
                    for (int col = 0; col < width; ++col) {
                        const auto dst = (static_cast<std::size_t>(placed[i].y[level] + row) * kAtlasWidth +
                                             static_cast<std::size_t>(placed[i].x[level] + col)) * 4 + 3;
                        loaded->rgba[dst] = coverage[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(col)];
                    }
                }
            }
            loaded->glyphs.emplace(boxes[i].glyph->code, placed[i]);
        }
        return loaded;
    }

    // The bytes of every fontlib in fontconfig.txt, in its order.
    using Libraries = std::vector<std::pair<std::string, std::vector<std::uint8_t>>>;

    // Game thread (BSResource): reads the libraries once for all faces.
    Libraries ReadLibraries(const NT::SwfFont::FontConfig& a_config)
    {
        Libraries libs;
        for (const auto& lib : a_config.libs) {
            libs.emplace_back(lib, ReadFile(lib));
        }
        return libs;
    }

    // The font name fontconfig.txt maps a_face to; empty when unmapped.
    std::string MappedName(std::string_view a_face, const NT::SwfFont::FontConfig& a_config)
    {
        const auto key = NT::Text::Lower(a_face);
        for (const auto& [alias, name] : a_config.maps) {
            if (NT::Text::Lower(alias) == key) {
                return name;
            }
        }
        return {};
    }

    // Any thread, no lock: parses and rasterizes font a_fontName from the
    // libraries. Null when no library defines it or reading it failed.
    std::unique_ptr<LoadedFont> LoadFace(std::string_view a_face, const std::string& a_fontName, const Libraries& a_libs)
    {
        try {
            for (const auto& [lib, data] : a_libs) {
                if (auto font = NT::SwfFont::Load(data, a_fontName)) {
                    auto loaded = BuildAtlas(a_face, *font);
                    logs::info("Icon font {}: {} glyph(s) from {} ({}), atlas {}x{}", a_face, loaded->glyphs.size(), lib, a_fontName, kAtlasWidth,
                        loaded->atlasHeight);
                    return loaded;
                }
            }
            logs::warn("Icon font {}: font '{}' not found in any fontlib", a_face, a_fontName);
        } catch (const std::exception& e) {
            logs::error("Icon font {}: reading failed ({})", a_face, e.what());
        }
        return nullptr;
    }

    // Set while Preload's worker builds the atlases; Has, Get and Found wait
    // for it outside g_lock instead of loading the same face a second time.
    // A promise, not std::async: that future's destructor would join the
    // worker at unload, after the process already ended it.
    std::shared_future<void> g_preload;

    void WaitForPreload()
    {
        std::shared_future<void> preload;
        {
            std::scoped_lock lock{ g_lock };
            preload = g_preload;
        }
        if (preload.valid()) {
            preload.wait();
        }
    }

    // Caller holds g_lock. The font for a_face, read on first use (normally
    // at Preload); null when it is not available.
    LoadedFont* Font(std::string_view a_face)
    {
        const auto key = NT::Text::Lower(a_face);
        if (const auto it = g_fonts.find(key); it != g_fonts.end()) {
            return it->second.get();
        }
        const auto& config = Config();
        auto&       slot = g_fonts[key];
        // Any face named like an icon font gets here; an unmapped one must
        // not read the libraries.
        if (const auto fontName = MappedName(a_face, config); fontName.empty()) {
            logs::info("Icon font {}: not mapped in fontconfig.txt", a_face);
        } else {
            slot = LoadFace(a_face, fontName, ReadLibraries(config));
        }
        return slot.get();
    }

    // Caller holds g_lock; render thread.
    bool CreateTexture(LoadedFont& a_font)
    {
        if (a_font.view || a_font.textureFailed) {
            return a_font.view != nullptr;
        }
        // Failed until proven otherwise; the CPU pixels go either way.
        a_font.textureFailed = true;
        const auto releasePixels = [&]() {
            a_font.rgba.clear();
            a_font.rgba.shrink_to_fit();
        };
        auto* device = RE::BSGraphics::Renderer::GetDevice();
        if (!device || a_font.atlasHeight <= 0) {
            releasePixels();
            return false;
        }
        REX::W32::D3D11_TEXTURE2D_DESC desc{};
        desc.width = kAtlasWidth;
        desc.height = static_cast<std::uint32_t>(a_font.atlasHeight);
        desc.mipLevels = 1;
        desc.arraySize = 1;
        desc.format = REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.sampleDesc.count = 1;
        desc.usage = REX::W32::D3D11_USAGE_IMMUTABLE;
        desc.bindFlags = REX::W32::D3D11_BIND_SHADER_RESOURCE;
        REX::W32::D3D11_SUBRESOURCE_DATA data{ a_font.rgba.data(), kAtlasWidth * 4, 0 };
        REX::W32::ID3D11Texture2D*       texture = nullptr;
        if (device->CreateTexture2D(&desc, &data, &texture) < 0 || !texture) {
            logs::error("Icon font {}: texture creation failed", a_font.face);
            releasePixels();
            return false;
        }
        const auto result = device->CreateShaderResourceView(texture, nullptr, &a_font.view);
        texture->Release();  // the view keeps it alive
        if (result < 0 || !a_font.view) {
            a_font.view = nullptr;
            logs::error("Icon font {}: shader resource view failed", a_font.face);
            releasePixels();
            return false;
        }
        a_font.textureFailed = false;
        releasePixels();
        return true;
    }
}

namespace NT::IconFont
{
    void Preload()
    {
        std::scoped_lock lock{ g_lock };
        if (g_preload.valid() && g_preload.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        const auto&              config = Config();
        std::vector<std::pair<std::string, std::string>> faces;  // alias, font name
        for (const auto& [alias, name] : config.maps) {
            if (NT::Markup::IsIconFace(alias) && !g_fonts.contains(NT::Text::Lower(alias))) {
                faces.emplace_back(alias, name);
            }
        }
        if (faces.empty()) {
            return;
        }
        // Reading stays here (BSResource); parsing and rasterizing (about
        // 0.3 s with OII) run on a worker so the main menu is not held up.
        auto               libs = ReadLibraries(config);
        std::promise<void> done;
        g_preload = done.get_future().share();
        std::thread([faces = std::move(faces), libs = std::move(libs), done = std::move(done)]() mutable {
            std::vector<std::pair<std::string, std::unique_ptr<LoadedFont>>> loaded;
            for (const auto& [face, fontName] : faces) {
                loaded.emplace_back(NT::Text::Lower(face), LoadFace(face, fontName, libs));
            }
            {
                std::scoped_lock lock{ g_lock };
                for (auto& [key, font] : loaded) {
                    g_fonts.try_emplace(key, std::move(font));
                }
            }
            done.set_value();
        }).detach();
    }

    bool Has(std::string_view a_face, char32_t a_code)
    {
        WaitForPreload();
        std::scoped_lock lock{ g_lock };
        const auto*      font = Font(a_face);
        return font && !font->textureFailed && font->glyphs.contains(a_code);
    }

    std::optional<GlyphImage> Get(std::string_view a_face, char32_t a_code, float a_pixelHeight)
    {
        WaitForPreload();
        std::scoped_lock lock{ g_lock };
        auto*            font = Font(a_face);
        if (!font) {
            return std::nullopt;
        }
        const auto it = font->glyphs.find(a_code);
        if (it == font->glyphs.end() || !CreateTexture(*font)) {
            return std::nullopt;
        }
        const auto level = NT::SwfFont::PickLevel(kLevels, a_pixelHeight);
        const auto& glyph = it->second;
        const float w = kAtlasWidth;
        const float h = static_cast<float>(font->atlasHeight);
        GlyphImage  image;
        image.texture = reinterpret_cast<ImGuiMCP::ImTextureID>(font->view);
        image.uv0 = { static_cast<float>(glyph.x[level]) / w, static_cast<float>(glyph.y[level]) / h };
        image.uv1 = { static_cast<float>(glyph.x[level] + glyph.width[level]) / w, static_cast<float>(glyph.y[level] + kLevels[level]) / h };
        image.aspect = glyph.aspect;
        return image;
    }

    std::vector<std::pair<std::string, std::size_t>> Found()
    {
        WaitForPreload();
        std::scoped_lock                                 lock{ g_lock };
        std::vector<std::pair<std::string, std::size_t>> found;
        const auto&                                      maps = Config().maps;
        for (const auto& [alias, name] : maps) {
            if (!NT::Markup::IsIconFace(alias)) {
                continue;
            }
            if (const auto* font = Font(alias); font && !font->textureFailed) {
                found.emplace_back(alias, font->glyphs.size());
            }
        }
        return found;
    }
}
