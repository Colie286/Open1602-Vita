// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/font.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace o1602::ui {

namespace {

constexpr std::uint8_t kBody = 1;
constexpr std::uint8_t kShadow = 7;
constexpr std::uint32_t kOverhang = 2;     // glyphs set pixels up to two columns right of their width
constexpr std::uint32_t kNoWrap = 10'000;  // from this width on nothing is wrapped (0x00401610)

constexpr std::array<TextColour, kTextColours> kColours = {{
    {{255, 255, 255}, {0, 0, 0}},
    {{221, 218, 181}, {0, 0, 0}},
    {{255, 255, 255}, {221, 218, 181}},
    {{255, 255, 0}, {0, 0, 0}},
    {{240, 240, 240}, {0, 0, 0}},
    {{105, 35, 9}, {119, 126, 60}},
    {{210, 195, 15}, {25, 15, 9}},
    {{240, 230, 190}, {25, 15, 9}},
    {{115, 51, 12}, {129, 131, 64}},
    {{235, 0, 0}, {88, 35, 9}},
    // Open1602's inks on parchment
    {{48, 30, 12}, {222, 206, 166}},
    {{110, 85, 55}, {214, 194, 150}},
    {{0, 120, 30}, {222, 206, 166}},
    {{170, 20, 10}, {222, 206, 166}},
    // Open1602's bright colours over the original's art
    {{100, 240, 70}, {0, 0, 0}},
    {{255, 90, 60}, {0, 0, 0}},
}};

bool control(char c) noexcept {
    return static_cast<unsigned char>(c) < 0x20;
}

std::string without_hyphens(std::string_view word) {
    std::string out;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (word[i] == '-' && i + 1 < word.size() && word[i + 1] == '-') {
            ++i;
            continue;
        }
        out += word[i];
    }
    return out;
}

}  // namespace

const TextColour& text_colour(std::size_t index) noexcept {
    return kColours[index < kColours.size() ? index : 0];
}

std::uint8_t nearest_colour(const assets::Palette& palette, std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    std::uint8_t best = 0;
    std::int32_t best_d = std::numeric_limits<std::int32_t>::max();
    for (std::size_t i = 0; i < assets::Palette::kSize; ++i) {
        const assets::Rgba& c = palette[static_cast<std::uint8_t>(i)];
        const std::int32_t dr = c.r - r;
        const std::int32_t dg = c.g - g;
        const std::int32_t db = c.b - b;
        const std::int32_t d = dr * dr + dg * dg + db * db;
        if (d < best_d) {
            best_d = d;
            best = static_cast<std::uint8_t>(i);
        }
    }
    return best;
}

namespace {

double luminance(const assets::Rgba& c) noexcept {
    const auto channel = [](std::uint8_t v) {
        const double x = v / 255.0;
        return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

}  // namespace

double contrast(const assets::Rgba& a, const assets::Rgba& b) noexcept {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

assets::Rgba legible_on(const assets::Rgba& colour, const assets::Rgba& ground, double min_contrast) noexcept {
    // A twentieth of the way to white at a time: few steps, and no more lightening than needed.
    for (int step = 0; step <= 20; ++step) {
        const auto mix = [step](std::uint8_t v) {
            return static_cast<std::uint8_t>(std::lround(v + (255.0 - v) * step / 20.0));
        };
        const assets::Rgba c{mix(colour.r), mix(colour.g), mix(colour.b), colour.a};
        if (contrast(c, ground) >= min_contrast) {
            return c;
        }
    }
    return assets::Rgba{255, 255, 255, colour.a};
}

Recolour text_recolour(const assets::Palette& palette, std::size_t index) {
    Recolour r{};
    for (std::size_t i = 0; i < r.size(); ++i) {
        r[i] = static_cast<std::uint8_t>(i);
    }
    const TextColour& c = text_colour(index);
    r[kBody] = nearest_colour(palette, c.body[0], c.body[1], c.body[2]);
    r[kShadow] = nearest_colour(palette, c.shadow[0], c.shadow[1], c.shadow[2]);
    return r;
}

Recolour text_recolour(const assets::Palette& palette, std::size_t index, std::uint8_t body) {
    Recolour r = text_recolour(palette, index);
    r[kBody] = body;
    return r;
}

core::Result<Font> Font::load(const assets::GameDirectory& game, std::string_view file) {
    const auto path = game.resolve(file);
    if (!path) {
        return core::make_error(core::ErrorCode::NotFound, std::string(file) + " not found");
    }
    auto sheet = assets::SpriteSheet::load(*path);
    if (!sheet) {
        return sheet.error();
    }
    return from_sheet(*sheet);
}

core::Result<Font> Font::from_sheet(const assets::SpriteSheet& sheet) {
    Font font;
    font.m_glyphs.reserve(sheet.size());
    for (std::size_t i = 0; i < sheet.size(); ++i) {
        auto image = sheet.decode(i, kOverhang);
        if (!image) {
            return image.error();
        }
        font.m_height = std::max(font.m_height, image->height);
        Glyph g;
        g.advance = sheet.info(i).width;
        g.image = std::move(image).value();
        font.m_glyphs.push_back(std::move(g));
    }
    if (font.m_glyphs.empty()) {
        return core::make_error(core::ErrorCode::InvalidFormat, "font without glyphs");
    }
    // The middle of the digits' ink, which every font has, above the bottom of the cell.
    double drops = 0.0;
    int digits = 0;
    for (unsigned char c = '0'; c <= '9' && font.has(c); ++c) {
        const assets::IndexedImage& image = font.m_glyphs[c - 0x20U].image;
        std::uint32_t top = image.height;
        std::uint32_t bottom = 0;
        for (std::uint32_t y = 0; y < image.height; ++y) {
            const auto row = image.opaque.begin() + static_cast<std::ptrdiff_t>(std::size_t{y} * image.width);
            if (std::any_of(row, row + image.width, [](std::uint8_t o) { return o != 0; })) {
                top = std::min(top, y);
                bottom = y + 1;
            }
        }
        if (top < bottom) {
            drops += image.height - (top + bottom) / 2.0;
            ++digits;
        }
    }
    font.m_ink_drop = digits > 0 ? drops / digits : font.m_height / 2.0;
    return font;
}

std::size_t Font::fix_accents() {
    // À-Å, È-Ï, Ñ-Ö, Ù-Ý (Windows-1252): the capitals with a mark above them.
    constexpr std::array<std::pair<unsigned, unsigned>, 4> kCapitals = {
        {{0xC0, 0xC5}, {0xC8, 0xCF}, {0xD1, 0xD6}, {0xD9, 0xDD}}};
    std::size_t changed = 0;
    for (const auto& [first, last] : kCapitals) {
        for (unsigned c = first; c <= last && has(static_cast<unsigned char>(c)); ++c) {
            assets::IndexedImage& image = m_glyphs[c - 0x20U].image;
            const auto row_set = [&](std::uint32_t y) {
                const auto row = image.opaque.begin() + static_cast<std::ptrdiff_t>(std::size_t{y} * image.width);
                return std::any_of(row, row + image.width, [](std::uint8_t o) { return o != 0; });
            };
            if (image.height != m_height) {
                continue;  // already moved
            }
            std::uint32_t rows = 0;  // the set rows at the bottom of the cell
            while (rows < image.height && row_set(image.height - 1 - rows)) {
                ++rows;
            }
            if (rows == 0 || rows == image.height) {
                continue;  // nothing there, or no empty row between them and the letter
            }
            // The image becomes `rows` taller: those rows on top of the cell, their place in it empty.
            const std::size_t moved = std::size_t{rows} * image.width;
            const auto split = static_cast<std::ptrdiff_t>(image.indices.size() - moved);
            std::rotate(image.indices.begin(), image.indices.begin() + split, image.indices.end());
            std::rotate(image.opaque.begin(), image.opaque.begin() + split, image.opaque.end());
            image.indices.resize(image.indices.size() + moved, 0);
            image.opaque.resize(image.opaque.size() + moved, 0);
            image.height += rows;
            m_above = std::max(m_above, rows);
            ++changed;
        }
    }
    return changed;
}

namespace {

/// The byte whose glyph shows a character: the no-break space (0xA0, which wrap_text() does not
/// break at: „33 %“) looks like a space.
unsigned char glyph_code(char ch) noexcept {
    const auto c = static_cast<unsigned char>(ch);
    return c == 0xA0 ? static_cast<unsigned char>(' ') : c;
}

}  // namespace

std::uint32_t Font::width(std::string_view text) const noexcept {
    std::uint32_t w = 0;
    for (const char ch : text) {
        const unsigned char c = glyph_code(ch);
        if (c < 0x20) {
            break;
        }
        if (has(c)) {
            w += m_glyphs[c - 0x20U].advance;
        }
    }
    return w;
}

std::uint32_t Font::draw(assets::IndexedImage& canvas, std::string_view text, std::int32_t x, std::int32_t bottom,
                         const Recolour& colour) const {
    const std::int32_t start = x;
    for (const char ch : text) {
        const unsigned char c = glyph_code(ch);
        if (c < 0x20) {
            break;
        }
        if (!has(c)) {
            continue;  // the original reads beyond the large fonts' 33 glyphs (T-015)
        }
        const Glyph& g = m_glyphs[c - 0x20U];
        const std::int32_t top = bottom - static_cast<std::int32_t>(g.image.height);
        for (std::uint32_t gy = 0; gy < g.image.height; ++gy) {
            const std::int32_t cy = top + static_cast<std::int32_t>(gy);
            if (cy < 0 || cy >= static_cast<std::int32_t>(canvas.height)) {
                continue;
            }
            for (std::uint32_t gx = 0; gx < g.image.width; ++gx) {
                const std::size_t gi = std::size_t{gy} * g.image.width + gx;
                const std::int32_t cx = x + static_cast<std::int32_t>(gx);
                if (g.image.opaque[gi] == 0 || cx < 0 || cx >= static_cast<std::int32_t>(canvas.width)) {
                    continue;
                }
                const std::size_t ci = static_cast<std::size_t>(cy) * canvas.width + static_cast<std::size_t>(cx);
                canvas.indices[ci] = colour[g.image.indices[gi]];
                canvas.opaque[ci] = 1;
            }
        }
        x += static_cast<std::int32_t>(g.advance);
    }
    return static_cast<std::uint32_t>(x - start);
}

std::uint32_t draw_text(assets::IndexedImage& canvas, const Font& font, std::string_view text, std::int32_t x,
                        std::int32_t bottom, const Recolour& colour, Align align) {
    const auto w = static_cast<std::int32_t>(font.width(text));
    const std::int32_t left = align == Align::Centre ? x - w / 2 : align == Align::Right ? x - w : x;
    return font.draw(canvas, text, left, bottom, colour);
}

std::vector<std::string> wrap_text(const Font& font, std::string_view text, std::uint32_t max_width) {
    std::vector<std::string> lines;
    const bool wrap = max_width < kNoWrap;
    std::size_t i = 0;
    while (i < text.size()) {
        std::string line;
        while (i < text.size()) {
            if (control(text[i])) {
                // A control character ends the line; several in a row count as one.
                while (i < text.size() && control(text[i])) {
                    ++i;
                }
                break;
            }
            std::size_t j = i;
            while (j < text.size() && text[j] != ' ' && !control(text[j])) {
                ++j;
            }
            const std::string_view word = text.substr(i, j - i);
            const std::string sep = line.empty() || line.back() == '-' ? "" : " ";
            const std::string candidate = line + sep + without_hyphens(word);
            if (!wrap || font.width(candidate) <= max_width) {
                line = candidate;
                i = j < text.size() && text[j] == ' ' ? j + 1 : j;
                continue;
            }
            // The word does not fit: break it at the last "--" that still fits, with a hyphen.
            std::size_t cut = std::string_view::npos;
            for (std::size_t k = word.find("--"); k != std::string_view::npos; k = word.find("--", k + 2)) {
                if (font.width(line + sep + without_hyphens(word.substr(0, k)) + "-") <= max_width) {
                    cut = k;
                }
            }
            if (cut != std::string_view::npos) {
                line += sep + without_hyphens(word.substr(0, cut)) + "-";
                i += cut + 2;
                break;
            }
            if (line.empty()) {
                // Wider than a whole line: as many characters as fit, at least one (0x004018A0).
                std::size_t n = 1;
                while (n < word.size() && font.width(std::string(word.substr(0, n + 1))) <= max_width) {
                    ++n;
                }
                line.assign(word.substr(0, n));
                i += n;
            }
            break;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

}  // namespace o1602::ui
