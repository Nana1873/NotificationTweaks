#include "SwfFont.h"

#include <zlib.h>

namespace
{
    // SWF bit fields are packed MSB first.
    class BitReader
    {
    public:
        BitReader(std::span<const std::uint8_t> a_data, std::size_t a_pos) :
            _data(a_data), _bit(a_pos * 8)
        {}

        std::uint32_t Unsigned(int a_bits)
        {
            std::uint32_t value = 0;
            for (int i = 0; i < a_bits; ++i) {
                const auto byte = _bit / 8;
                if (byte >= _data.size()) {
                    _overrun = true;
                    return 0;
                }
                value = (value << 1) | ((_data[byte] >> (7 - _bit % 8)) & 1U);
                ++_bit;
            }
            return value;
        }

        std::int32_t Signed(int a_bits)
        {
            const auto value = Unsigned(a_bits);
            if (a_bits > 0 && a_bits < 32 && (value & (1U << (a_bits - 1)))) {
                return static_cast<std::int32_t>(value | (~0U << a_bits));
            }
            return static_cast<std::int32_t>(value);
        }

        void SkipRect()
        {
            const int bits = static_cast<int>(Unsigned(5));
            Unsigned(bits);
            Unsigned(bits);
            Unsigned(bits);
            Unsigned(bits);
        }

        std::size_t BytePos() const { return (_bit + 7) / 8; }
        bool        Overrun() const { return _overrun; }

    private:
        std::span<const std::uint8_t> _data;
        std::size_t                   _bit;
        bool                          _overrun = false;
    };

    std::uint16_t U16(std::span<const std::uint8_t> a_data, std::size_t a_pos)
    {
        return a_pos + 2 <= a_data.size() ? static_cast<std::uint16_t>(a_data[a_pos] | (a_data[a_pos + 1] << 8)) : 0;
    }

    std::uint32_t U32(std::span<const std::uint8_t> a_data, std::size_t a_pos)
    {
        return a_pos + 4 <= a_data.size() ?
                   static_cast<std::uint32_t>(a_data[a_pos] | (a_data[a_pos + 1] << 8) | (a_data[a_pos + 2] << 16) | (a_data[a_pos + 3] << 24)) :
                   0;
    }

    std::string Lower(std::string_view a_text)
    {
        std::string out(a_text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    // The text between the next pair of double quotes after a_pos.
    std::optional<std::string> Quoted(std::string_view a_line, std::size_t& a_pos)
    {
        const auto open = a_line.find('"', a_pos);
        if (open == std::string_view::npos) {
            return std::nullopt;
        }
        const auto close = a_line.find('"', open + 1);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        a_pos = close + 1;
        return std::string(a_line.substr(open + 1, close - open - 1));
    }

    // SWF shape records of one glyph (a_data is exactly its slice of the
    // shape table), curves flattened, in font units.
    std::vector<std::vector<NT::SwfFont::Point>> ReadShape(std::span<const std::uint8_t> a_data)
    {
        constexpr int         kCurveSteps = 8;
        constexpr std::size_t kMaxPoints = 50000;  // far beyond any real glyph; stops hostile files

        std::vector<std::vector<NT::SwfFont::Point>> contours;
        std::size_t                                  points = 0;
        BitReader                                    bits(a_data, 0);
        const int                                    fillBits = static_cast<int>(bits.Unsigned(4));
        const int                                    lineBits = static_cast<int>(bits.Unsigned(4));
        float                                        x = 0.0F;
        float                                        y = 0.0F;

        const auto add = [&](float a_x, float a_y) {
            if (contours.empty()) {
                contours.push_back({ { x, y } });
            }
            contours.back().push_back({ a_x, a_y });
            ++points;
            x = a_x;
            y = a_y;
        };

        while (!bits.Overrun() && points < kMaxPoints) {
            if (bits.Unsigned(1) == 0) {
                const auto flags = bits.Unsigned(5);  // newStyles, lineStyle, fill1, fill0, moveTo
                if (flags == 0 || (flags & 0x10)) {
                    break;  // end of shape (fonts never define new styles)
                }
                if (flags & 0x01) {
                    const int moveBits = static_cast<int>(bits.Unsigned(5));
                    x = static_cast<float>(bits.Signed(moveBits));
                    y = static_cast<float>(bits.Signed(moveBits));
                    contours.push_back({ { x, y } });
                    ++points;
                }
                if (flags & 0x02) {
                    bits.Unsigned(fillBits);
                }
                if (flags & 0x04) {
                    bits.Unsigned(fillBits);
                }
                if (flags & 0x08) {
                    bits.Unsigned(lineBits);
                }
            } else if (bits.Unsigned(1) == 1) {  // straight edge
                const int numBits = static_cast<int>(bits.Unsigned(4)) + 2;
                float     dx = 0.0F;
                float     dy = 0.0F;
                if (bits.Unsigned(1) == 1) {
                    dx = static_cast<float>(bits.Signed(numBits));
                    dy = static_cast<float>(bits.Signed(numBits));
                } else if (bits.Unsigned(1) == 1) {
                    dy = static_cast<float>(bits.Signed(numBits));
                } else {
                    dx = static_cast<float>(bits.Signed(numBits));
                }
                add(x + dx, y + dy);
            } else {  // quadratic curve
                const int   numBits = static_cast<int>(bits.Unsigned(4)) + 2;
                const float cx = x + static_cast<float>(bits.Signed(numBits));
                const float cy = y + static_cast<float>(bits.Signed(numBits));
                const float ax = cx + static_cast<float>(bits.Signed(numBits));
                const float ay = cy + static_cast<float>(bits.Signed(numBits));
                const float sx = x;
                const float sy = y;
                for (int i = 1; i <= kCurveSteps; ++i) {
                    const float t = static_cast<float>(i) / kCurveSteps;
                    const float u = 1.0F - t;
                    add(u * u * sx + 2 * u * t * cx + t * t * ax, u * u * sy + 2 * u * t * cy + t * t * ay);
                }
            }
        }
        return contours;
    }

    // DefineFont2 (48) / DefineFont3 (75) tag body.
    std::optional<NT::SwfFont::Font> ReadDefineFont(std::span<const std::uint8_t> a_tag, bool a_font3, std::string_view a_wanted)
    {
        if (a_tag.size() < 6) {
            return std::nullopt;
        }
        const std::uint8_t flags = a_tag[2];
        const bool         hasLayout = flags & 0x80;
        const bool         wideOffsets = flags & 0x08;
        const bool         wideCodes = flags & 0x04;
        const std::size_t  nameLen = a_tag[4];
        if (5 + nameLen + 2 > a_tag.size()) {
            return std::nullopt;
        }
        std::string name(reinterpret_cast<const char*>(a_tag.data() + 5), nameLen);
        name.erase(std::ranges::find(name, '\0'), name.end());
        if (Lower(name) != Lower(a_wanted)) {
            return std::nullopt;
        }

        // DefineFont3 coordinates are 1/20 of the 1024-unit EM square.
        constexpr std::size_t kMaxGlyphs = 8192;

        const float       unitsPerEm = a_font3 ? 1024.0F * 20.0F : 1024.0F;
        const std::size_t numGlyphs = U16(a_tag, 5 + nameLen);
        if (numGlyphs == 0 || numGlyphs > kMaxGlyphs) {
            return std::nullopt;
        }
        const std::size_t table = 5 + nameLen + 2;
        const std::size_t offsetSize = wideOffsets ? 4 : 2;
        const auto        offset = [&](std::size_t a_index) {
            return wideOffsets ? U32(a_tag, table + a_index * 4) : U16(a_tag, table + a_index * 2);
        };
        const std::size_t codeTable = table + offset(numGlyphs);
        const std::size_t codeSize = wideCodes ? 2 : 1;
        if (table + (numGlyphs + 1) * offsetSize > a_tag.size() || codeTable + numGlyphs * codeSize > a_tag.size()) {
            return std::nullopt;
        }

        NT::SwfFont::Font font;
        font.name = name;
        font.glyphs.resize(numGlyphs);
        for (std::size_t i = 0; i < numGlyphs; ++i) {
            auto& glyph = font.glyphs[i];
            glyph.code = wideCodes ? U16(a_tag, codeTable + i * 2) : a_tag[codeTable + i];
            // Each shape ends where the next one (or the code table) starts.
            const std::size_t begin = table + offset(i);
            const std::size_t end = table + offset(i + 1);
            if (begin < end && end <= a_tag.size()) {
                glyph.contours = ReadShape(a_tag.subspan(begin, end - begin));
            }
            for (auto& contour : glyph.contours) {
                for (auto& point : contour) {
                    point.x /= unitsPerEm;
                    point.y /= unitsPerEm;
                }
            }
        }

        const std::size_t layout = codeTable + numGlyphs * codeSize;
        if (hasLayout && layout + 6 + numGlyphs * 2 <= a_tag.size()) {
            font.ascent = static_cast<float>(U16(a_tag, layout)) / unitsPerEm;
            font.descent = static_cast<float>(U16(a_tag, layout + 2)) / unitsPerEm;
            for (std::size_t i = 0; i < numGlyphs; ++i) {
                font.glyphs[i].advance = static_cast<float>(static_cast<std::int16_t>(U16(a_tag, layout + 6 + i * 2))) / unitsPerEm;
            }
        }
        for (auto& glyph : font.glyphs) {
            if (glyph.advance <= 0.0F) {  // no layout: the outline's right edge
                for (const auto& contour : glyph.contours) {
                    for (const auto& point : contour) {
                        glyph.advance = std::max(glyph.advance, point.x);
                    }
                }
            }
        }
        if (font.ascent + font.descent <= 0.0F) {
            font.ascent = 0.8F;
            font.descent = 0.2F;
        }
        return font;
    }
}

namespace NT::SwfFont
{
    FontConfig ParseFontConfig(std::string_view a_text)
    {
        FontConfig config;
        std::size_t start = 0;
        while (start < a_text.size()) {
            auto end = a_text.find('\n', start);
            if (end == std::string_view::npos) {
                end = a_text.size();
            }
            auto line = a_text.substr(start, end - start);
            start = end + 1;
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                line.remove_prefix(1);
            }
            const auto  lower = Lower(line.substr(0, 8));
            std::size_t pos = 0;
            if (lower.starts_with("fontlib")) {
                if (auto lib = Quoted(line, pos)) {
                    config.libs.push_back(std::move(*lib));
                }
            } else if (lower.starts_with("map")) {
                auto alias = Quoted(line, pos);
                auto font = Quoted(line, pos);
                if (alias && font) {
                    config.maps.emplace_back(std::move(*alias), std::move(*font));
                }
            }
        }
        return config;
    }

    std::optional<Font> Load(std::span<const std::uint8_t> a_swf, std::string_view a_name)
    {
        if (a_swf.size() < 8 || a_swf[1] != 'W' || a_swf[2] != 'S') {
            return std::nullopt;
        }
        std::vector<std::uint8_t>     inflated;
        std::span<const std::uint8_t> body = a_swf.subspan(8);
        if (a_swf[0] == 'C') {
            const auto length = U32(a_swf, 4);
            if (length < 8 || length > 64U * 1024 * 1024) {
                return std::nullopt;
            }
            inflated.resize(length - 8);
            uLongf size = static_cast<uLongf>(inflated.size());
            const int result = uncompress(inflated.data(), &size, a_swf.data() + 8, static_cast<uLong>(a_swf.size() - 8));
            if (result != Z_OK && result != Z_BUF_ERROR) {
                return std::nullopt;
            }
            inflated.resize(size);
            body = inflated;
        } else if (a_swf[0] != 'F') {
            return std::nullopt;  // LZMA (ZWS) is not used by game font libraries
        }

        BitReader header(body, 0);
        header.SkipRect();
        std::size_t pos = header.BytePos() + 4;  // frame rate, frame count
        while (pos + 2 <= body.size()) {
            const auto  codeAndLength = U16(body, pos);
            const auto  code = codeAndLength >> 6;
            std::size_t length = codeAndLength & 0x3F;
            pos += 2;
            if (length == 0x3F) {
                length = U32(body, pos);
                pos += 4;
            }
            if (code == 0 || pos + length > body.size()) {
                break;
            }
            if (code == 48 || code == 75) {
                if (auto font = ReadDefineFont(body.subspan(pos, length), code == 75, a_name)) {
                    return font;
                }
            }
            pos += length;
        }
        return std::nullopt;
    }

    std::vector<std::uint8_t> Rasterize(const std::vector<std::vector<Point>>& a_contours, int a_width, int a_height, float a_scale,
        float a_originX, float a_originY)
    {
        constexpr int kSamples = 4;  // sub-scanlines per pixel row

        std::vector<std::uint8_t> out(static_cast<std::size_t>(a_width) * static_cast<std::size_t>(a_height), 0);
        std::vector<float>        row(static_cast<std::size_t>(a_width));
        std::vector<float>        crossings;
        for (int py = 0; py < a_height; ++py) {
            std::ranges::fill(row, 0.0F);
            for (int s = 0; s < kSamples; ++s) {
                const float sy = static_cast<float>(py) + (static_cast<float>(s) + 0.5F) / kSamples;
                crossings.clear();
                for (const auto& contour : a_contours) {
                    for (std::size_t i = 0; i < contour.size(); ++i) {
                        const auto& a = contour[i];
                        const auto& b = contour[(i + 1) % contour.size()];  // contours close implicitly
                        const float ay = a.y * a_scale + a_originY;
                        const float by = b.y * a_scale + a_originY;
                        if ((ay <= sy && sy < by) || (by <= sy && sy < ay)) {
                            const float ax = a.x * a_scale + a_originX;
                            const float bx = b.x * a_scale + a_originX;
                            crossings.push_back(ax + (sy - ay) * (bx - ax) / (by - ay));
                        }
                    }
                }
                std::ranges::sort(crossings);
                for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                    const float x0 = std::clamp(crossings[i], 0.0F, static_cast<float>(a_width));
                    const float x1 = std::clamp(crossings[i + 1], 0.0F, static_cast<float>(a_width));
                    for (int px = static_cast<int>(x0); px < a_width && static_cast<float>(px) < x1; ++px) {
                        const float covered = std::min(x1, static_cast<float>(px + 1)) - std::max(x0, static_cast<float>(px));
                        row[static_cast<std::size_t>(px)] += std::max(covered, 0.0F) / kSamples;
                    }
                }
            }
            for (int px = 0; px < a_width; ++px) {
                out[static_cast<std::size_t>(py) * static_cast<std::size_t>(a_width) + static_cast<std::size_t>(px)] =
                    static_cast<std::uint8_t>(std::lround(std::clamp(row[static_cast<std::size_t>(px)], 0.0F, 1.0F) * 255.0F));
            }
        }
        return out;
    }
}
