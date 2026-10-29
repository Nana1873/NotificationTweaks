#pragma once

#include "SKSEMenuFramework.h"

// Icon fonts for the overlay: a <font face="$Iconographia"> glyph is drawn
// from the font library Interface/fontconfig.txt maps that face to (Oblivion
// Interaction Icons' Fonticon.swf, read from the player's install; nothing
// ships with the plugin). Without such a font nothing is loaded.
namespace NT::IconFont
{
    struct GlyphImage
    {
        ImGuiMCP::ImTextureID texture = nullptr;
        ImGuiMCP::ImVec2      uv0;
        ImGuiMCP::ImVec2      uv1;
        float                 aspect = 1.0F;  // width / height of the glyph box
    };

    // Game thread at kDataLoaded: reads fontconfig.txt and loads every icon
    // face it maps, so no font is read or rasterized during play.
    void Preload();

    // Any thread: whether a_face has a drawable glyph for a_code. False once
    // the face's texture failed, so such lines fall back to the Vanilla HUD.
    bool Has(std::string_view a_face, char32_t a_code);

    // Render thread: the glyph rasterized closest above a_pixelHeight
    // (creates the texture on first use). nullopt when unknown.
    std::optional<GlyphImage> Get(std::string_view a_face, char32_t a_code, float a_pixelHeight);

    // Menu: every icon face in fontconfig.txt with its glyph count
    // ("$Iconographia", 60). Empty without such a font.
    std::vector<std::pair<std::string, std::size_t>> Found();
}
