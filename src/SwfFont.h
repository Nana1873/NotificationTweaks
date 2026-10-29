#pragma once

// Glyph outlines from a Scaleform font library (.swf, listed as fontlib in
// Interface/fontconfig.txt) and a small rasterizer for them. Used to draw
// icon fonts such as Oblivion Interaction Icons' "$Iconographia", which
// ImGui cannot load. No game types here, so the unit tests cover it.
namespace NT::SwfFont
{
    struct FontConfig
    {
        std::vector<std::string>                         libs;  // fontlib "Interface\fonts_en.swf"
        std::vector<std::pair<std::string, std::string>> maps;  // map "$Iconographia" = "Iconographia"
    };

    FontConfig ParseFontConfig(std::string_view a_text);

    struct Point
    {
        float x = 0.0F;
        float y = 0.0F;
    };

    // Coordinates in EM units (1.0 = font size), y down, baseline at 0.
    struct Glyph
    {
        char32_t                        code = 0;
        float                           advance = 0.0F;
        std::vector<std::vector<Point>> contours;
    };

    struct Font
    {
        std::string        name;
        float              ascent = 0.8F;   // EM units above the baseline
        float              descent = 0.2F;  // EM units below it
        std::vector<Glyph> glyphs;
    };

    // Finds the DefineFont2/DefineFont3 named a_name (case-insensitive) in an
    // uncompressed (FWS) or zlib-compressed (CWS) SWF.
    std::optional<Font> Load(std::span<const std::uint8_t> a_swf, std::string_view a_name);

    // Index into a_heights (descending raster sizes) of the smallest height
    // still at least a_pixelHeight; the largest when none is.
    std::size_t PickLevel(std::span<const int> a_heights, float a_pixelHeight);

    // Even-odd coverage (0..255) of a_contours in an a_width x a_height
    // bitmap; a point maps to (x * a_scale + a_originX, y * a_scale + a_originY).
    std::vector<std::uint8_t> Rasterize(const std::vector<std::vector<Point>>& a_contours, int a_width, int a_height, float a_scale,
        float a_originX, float a_originY);
}
