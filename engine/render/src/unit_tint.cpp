// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/unit_tint.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "o1602/render/flag_tint.hpp"

namespace o1602::render {

std::optional<std::uint32_t> UnitTint::sprite(std::size_t banner, std::uint32_t image) const noexcept {
    if (banner >= banner_base.size() || !banner_base[banner]) {
        return std::nullopt;
    }
    for (const UnitImageGroup& g : groups) {
        if (image < g.first || image >= g.first + g.stride * g.variants) {
            continue;
        }
        return *banner_base[banner] + g.offset + (image - g.first) % g.stride;
    }
    return std::nullopt;
}

namespace {

/// Colour minus its grey part: the direction of its hue.
struct Chroma {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    Chroma() = default;
    Chroma(double red, double green, double blue) noexcept {
        const double mean = (red + green + blue) / 3.0;
        r = red - mean;
        g = green - mean;
        b = blue - mean;
    }
    explicit Chroma(const std::uint8_t* p) noexcept : Chroma(p[0], p[1], p[2]) {}
    [[nodiscard]] double length() const noexcept { return std::sqrt(r * r + g * g + b * b); }
    [[nodiscard]] double cosine(const Chroma& o) const noexcept {
        const double l = length() * o.length();
        return l > 0.0 ? (r * o.r + g * o.g + b * o.b) / l : 0.0;
    }
};

double luminance(const std::uint8_t* p) noexcept {
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
}

constexpr double kMinColourChroma = 24.0;  // player colours below this have no hue (white)
constexpr double kMinPixelChroma = 12.0;   // pixels below this are grey
constexpr double kMinHueCosine = 0.5;      // within 60 degrees of the player colour
constexpr double kMaxLightChroma = 30.0;   // white / light grey cloth of the base variant
constexpr double kMinLightLuminance = 170.0;

// Horses of the original riders: neutral in the white variant, grey or black in the blue one,
// brown (or dark) in the red one, dark in the yellow one.
constexpr double kHorseMaxNeutralChroma = 40.0;
constexpr double kHorseMinBrownChroma = 20.0;
constexpr double kHorseMinBrownCosine = 0.7;
constexpr double kHorseMaxDarkLuminance = 70.0;
constexpr double kHorseMaxYellowLuminance = 130.0;

/// Variant images aligned at their bottom centre in a frame of `width` x `height`.
class Aligned {
public:
    Aligned(std::span<const RgbaView> variants, std::uint32_t width, std::uint32_t height) : m_variants(variants) {
        for (const RgbaView& v : variants) {
            m_dx.push_back(static_cast<std::int64_t>(width / 2) - static_cast<std::int64_t>(v.width / 2));
            m_dy.push_back(static_cast<std::int64_t>(height) - static_cast<std::int64_t>(v.height));
        }
    }
    /// Pixel of variant v at frame position (x, y), nullptr if outside or transparent.
    [[nodiscard]] const std::uint8_t* at(std::size_t v, std::int64_t x, std::int64_t y) const noexcept {
        const RgbaView& view = m_variants[v];
        const std::int64_t vx = x - m_dx[v];
        const std::int64_t vy = y - m_dy[v];
        if (view.pixels == nullptr || vx < 0 || vy < 0 || vx >= view.width || vy >= view.height) {
            return nullptr;
        }
        const std::uint8_t* p =
            view.pixels + (static_cast<std::size_t>(vy) * view.width + static_cast<std::size_t>(vx)) * 4;
        return p[3] != 0 ? p : nullptr;
    }

private:
    std::span<const RgbaView> m_variants;
    std::vector<std::int64_t> m_dx;
    std::vector<std::int64_t> m_dy;
};

/// Square closing (dilate, then erode) with radius r; outside counts as empty.
std::vector<std::uint8_t> close_mask(const std::vector<std::uint8_t>& mask, std::uint32_t w, std::uint32_t h, int r) {
    const auto pass = [&](const std::vector<std::uint8_t>& in, bool dilate) {
        std::vector<std::uint8_t> out(in.size(), 0);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                bool any = false;
                bool all = true;
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        const std::int64_t nx = static_cast<std::int64_t>(x) + dx;
                        const std::int64_t ny = static_cast<std::int64_t>(y) + dy;
                        const bool v = nx >= 0 && ny >= 0 && nx < w && ny < h &&
                                       in[static_cast<std::size_t>(ny) * w + static_cast<std::size_t>(nx)] != 0;
                        any = any || v;
                        all = all && v;
                    }
                }
                out[std::size_t{y} * w + x] = (dilate ? any : all) ? 1 : 0;
            }
        }
        return out;
    };
    return pass(pass(mask, true), false);
}

/// Sets the enclosed holes of a mask (regions not connected to the border).
void fill_holes(std::vector<std::uint8_t>& mask, std::uint32_t w, std::uint32_t h) {
    if (w == 0 || h == 0) {
        return;
    }
    std::vector<std::uint8_t> outside(mask.size(), 0);
    std::vector<std::size_t> stack;
    const auto push = [&](std::int64_t x, std::int64_t y) {
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return;
        }
        const auto p = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);
        if (mask[p] == 0 && outside[p] == 0) {
            outside[p] = 1;
            stack.push_back(p);
        }
    };
    for (std::uint32_t x = 0; x < w; ++x) {
        push(x, 0);
        push(x, static_cast<std::int64_t>(h) - 1);
    }
    for (std::uint32_t y = 0; y < h; ++y) {
        push(0, y);
        push(static_cast<std::int64_t>(w) - 1, y);
    }
    while (!stack.empty()) {
        const std::size_t p = stack.back();
        stack.pop_back();
        const auto x = static_cast<std::int64_t>(p % w);
        const auto y = static_cast<std::int64_t>(p / w);
        push(x + 1, y);
        push(x - 1, y);
        push(x, y + 1);
        push(x, y - 1);
    }
    for (std::size_t p = 0; p < mask.size(); ++p) {
        mask[p] = outside[p] == 0 ? 1 : 0;
    }
}

}  // namespace

void unit_template(std::span<const RgbaView> variants, std::span<const assets::Rgba> colours,
                   const UnitTemplateOptions& options, std::vector<std::uint8_t>& out, std::uint32_t& width,
                   std::uint32_t& height) {
    const RgbaView& body = variants[options.body];
    width = body.width;
    height = body.height;
    const std::size_t n = std::size_t{width} * height;
    out.assign(n * 4, 0);
    if (n == 0) {
        return;
    }
    const Aligned aligned(variants, width, height);
    const Chroma horse_brown(137.0, 86.0, 13.0);

    // Hues of the player colours (none for white).
    std::vector<std::optional<Chroma>> hues(variants.size());
    for (std::size_t v = 0; v < variants.size() && v < colours.size(); ++v) {
        const Chroma c(colours[v].r, colours[v].g, colours[v].b);
        if (c.length() >= kMinColourChroma) {
            hues[v] = c;
        }
    }
    const auto shows_own_hue = [&](std::size_t v, const std::uint8_t* p, const std::uint8_t* base) {
        if (!hues[v] || p == nullptr || std::equal(p, p + 3, base)) {
            return false;
        }
        const Chroma c(p);
        if (c.length() < kMinPixelChroma) {
            return false;
        }
        const double own = c.cosine(*hues[v]);
        if (own < kMinHueCosine) {
            return false;
        }
        for (std::size_t o = 0; o < hues.size(); ++o) {
            if (o != v && hues[o] && c.cosine(*hues[o]) > own) {
                return false;
            }
        }
        return true;
    };

    std::vector<std::uint8_t> matches(n, 0);     // variants showing their colour
    std::vector<std::uint8_t> body_shows(n, 0);  // ... among them the body variant
    std::vector<std::uint8_t> light(n, 0);       // white or light grey in the base variant
    std::vector<std::uint8_t> horse(n, 0);       // horse of a rider
    const bool riders = options.mounted && variants.size() >= 4;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t p = std::size_t{y} * width + x;
            const std::uint8_t* base = aligned.at(options.base, x, y);
            if (base == nullptr) {
                continue;
            }
            for (std::size_t v = 0; v < variants.size(); ++v) {
                if (shows_own_hue(v, aligned.at(v, x, y), base)) {
                    ++matches[p];
                    if (v == options.body) {
                        body_shows[p] = 1;
                    }
                }
            }
            const double base_chroma = Chroma(base).length();
            light[p] = base_chroma <= kMaxLightChroma && luminance(base) >= kMinLightLuminance ? 1 : 0;
            if (riders) {
                // The original order: red, blue, yellow, white.
                const std::uint8_t* red = aligned.at(0, x, y);
                const std::uint8_t* blue = aligned.at(1, x, y);
                const std::uint8_t* yellow = aligned.at(2, x, y);
                if (red != nullptr && blue != nullptr && yellow != nullptr && base_chroma <= kHorseMaxNeutralChroma &&
                    Chroma(blue).length() < kHorseMaxNeutralChroma && luminance(yellow) < kHorseMaxYellowLuminance) {
                    const Chroma r(red);
                    const bool brown =
                        r.length() >= kHorseMinBrownChroma && r.cosine(horse_brown) >= kHorseMinBrownCosine;
                    horse[p] = brown || luminance(red) < kHorseMaxDarkLuminance ? 1 : 0;
                }
            }
        }
    }
    if (riders) {
        // Close the gaps between the horse pixels found and fill the enclosed rest (saddle, legs);
        // enclosed pixels where the body variant shows its own colour are its rider.
        std::vector<std::uint8_t> closed = close_mask(horse, width, height, 2);
        for (std::size_t p = 0; p < n; ++p) {
            closed[p] = closed[p] != 0 || horse[p] != 0 ? 1 : 0;  // the erosion eats the border
        }
        fill_holes(closed, width, height);
        for (std::size_t p = 0; p < n; ++p) {
            const bool base_opaque = aligned.at(options.base, static_cast<std::int64_t>(p % width),
                                                static_cast<std::int64_t>(p / width)) != nullptr;
            const bool keep = closed[p] != 0 && base_opaque && matches[p] < 2 && (horse[p] != 0 || body_shows[p] == 0);
            horse[p] = keep ? 1 : 0;
        }
    }

    // Cloth: seeds (two variants show their colour) grown into connected light pixels with one.
    std::vector<std::uint8_t> cloth(n, 0);
    std::vector<std::size_t> stack;
    for (std::size_t p = 0; p < n; ++p) {
        if (matches[p] >= 2 && horse[p] == 0) {
            cloth[p] = 1;
            stack.push_back(p);
        }
    }
    while (!stack.empty()) {
        const std::size_t p = stack.back();
        stack.pop_back();
        const auto x = static_cast<std::int64_t>(p % width);
        const auto y = static_cast<std::int64_t>(p / width);
        for (std::int64_t dy = -1; dy <= 1; ++dy) {
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                const std::int64_t nx = x + dx;
                const std::int64_t ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                    continue;
                }
                const auto q = static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx);
                if (cloth[q] == 0 && horse[q] == 0 && matches[q] >= 1 && light[q] != 0) {
                    cloth[q] = 1;
                    stack.push_back(q);
                }
            }
        }
    }

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t p = std::size_t{y} * width + x;
            const std::uint8_t* base = aligned.at(options.base, x, y);
            // The figure from the base variant; the horse and anything outside the base from the
            // body variant.
            const std::uint8_t* src = base != nullptr && horse[p] == 0 ? base : aligned.at(options.body, x, y);
            if (src == nullptr) {
                continue;
            }
            std::uint8_t* o = &out[p * 4];
            std::copy_n(src, 4, o);
            if (src != base && options.mount_brightness != 1.0) {
                for (int c = 0; c < 3; ++c) {
                    o[c] =
                        static_cast<std::uint8_t>(std::clamp(std::lround(o[c] * options.mount_brightness), 0L, 255L));
                }
            }
            if (cloth[p] != 0 && src == base) {
                const auto shade =
                    static_cast<std::uint8_t>(std::clamp(std::lround(0.48 * luminance(base) + 5.0), 0L, 255L));
                o[0] = o[1] = o[2] = shade;
            } else if (o[0] == o[1] && o[1] == o[2]) {
                o[2] = static_cast<std::uint8_t>(o[2] < 255 ? o[2] + 1 : 254);  // keep it out of the tint
            }
        }
    }
}

TintedUnitSource::TintedUnitSource(const SpriteSource& sheet, std::vector<UnitImageGroup> groups,
                                   std::vector<assets::Rgba> variant_colours, std::vector<UnitTintColour> colours)
    : m_sheet(sheet),
      m_groups(std::move(groups)),
      m_variant_colours(std::move(variant_colours)),
      m_colours(std::move(colours)) {
    for (UnitImageGroup& g : m_groups) {
        g.offset = m_block;
        m_block += g.stride;
    }
}

std::uint32_t TintedUnitSource::count() const {
    return static_cast<std::uint32_t>(m_colours.size()) * m_block;
}

const UnitImageGroup* TintedUnitSource::locate(std::uint32_t i, std::uint32_t& k) const noexcept {
    const std::uint32_t in_block = m_block == 0 ? 0 : i % m_block;
    for (const UnitImageGroup& g : m_groups) {
        if (in_block >= g.offset && in_block < g.offset + g.stride) {
            k = in_block - g.offset;
            return &g;
        }
    }
    return nullptr;
}

namespace {

constexpr std::uint8_t kMountBrown = 0;
constexpr std::size_t kMountBrownSource = 2;   // the yellow riders' dark brown horse ...
constexpr double kMountBrownBrightness = 1.5;  // ... made lighter

}  // namespace

std::size_t TintedUnitSource::body(const UnitImageGroup& g, std::uint32_t i) const noexcept {
    if (!g.mounted || m_block == 0 || g.variants == 0) {
        return g.base;
    }
    const std::uint8_t mount = m_colours[i / m_block].mount;
    if (mount == kMountBrown) {
        return std::min<std::size_t>(kMountBrownSource, g.variants - 1U);
    }
    return std::min<std::size_t>(mount, g.variants - 1U);
}

void TintedUnitSource::size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const {
    std::uint32_t k = 0;
    const UnitImageGroup* g = locate(i, k);
    if (g == nullptr) {
        width = height = 0;
        return;
    }
    m_sheet.size(g->first + static_cast<std::uint32_t>(body(*g, i)) * g->stride + k, width, height);
}

bool TintedUnitSource::pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const {
    std::uint32_t k = 0;
    const UnitImageGroup* g = locate(i, k);
    if (g == nullptr || g->variants == 0) {
        return false;
    }
    UnitTemplateOptions options;
    options.base = g->base;
    options.body = body(*g, i);
    options.mounted = g->mounted;
    const bool brown = g->mounted && m_colours[i / m_block].mount == kMountBrown;
    if (brown) {
        options.mount_brightness = kMountBrownBrightness;
    }
    const std::uint64_t key =
        std::uint64_t{g->offset + k} | (std::uint64_t{options.body} << 32U) | (std::uint64_t{brown} << 40U);
    std::shared_ptr<Template> t;
    {
        const std::scoped_lock lock(m_templates_mutex);
#ifdef O1602_EXPERIMENTAL_VITA
        // Keep exact templates, but bound their lifetime during large atlas builds.
        // Local shared ownership protects in-flight users when the cache is reset.
        if(m_template_bytes>4*1024*1024){m_templates.clear();m_template_bytes=0;}
#endif
        auto& slot = m_templates[key];
        if (!slot) {
            slot = std::make_shared<Template>();
        }
        t = slot;
    }
    std::call_once(t->made, [&] {
        t->ok = make_template(*g, k, options, t->rgba);
        const std::scoped_lock lock(m_templates_mutex);m_template_bytes+=t->rgba.size();
    });
    if (!t->ok) {
        return false;
    }
    rgba = t->rgba;
    tint_rgba(rgba, m_colours[i / m_block].colour);
    return true;
}

bool TintedUnitSource::make_template(const UnitImageGroup& g, std::uint32_t k, const UnitTemplateOptions& options,
                                     std::vector<std::uint8_t>& rgba) const {
    std::vector<std::vector<std::uint8_t>> images(g.variants);
    std::vector<RgbaView> views(g.variants);
    for (std::uint8_t v = 0; v < g.variants; ++v) {
        const std::uint32_t image = g.first + v * g.stride + k;
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        m_sheet.size(image, w, h);
        if (w > 0 && h > 0 && m_sheet.pixels(image, images[v])) {
            views[v] = RgbaView{images[v].data(), w, h};
        }
    }
    if (views[options.base].pixels == nullptr || views[options.body].pixels == nullptr) {
        return false;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    unit_template(views, m_variant_colours, options, rgba, w, h);
    return true;
}

TintedTemplateSource::TintedTemplateSource(const SpriteSource& sheet, std::vector<UnitImageGroup> groups,
                                           std::vector<assets::Rgba> colours)
    : m_sheet(sheet), m_groups(std::move(groups)), m_colours(std::move(colours)) {
    for (UnitImageGroup& g : m_groups) {
        g.offset = m_block;
        m_block += g.stride;
    }
}

std::uint32_t TintedTemplateSource::count() const {
    return m_block * static_cast<std::uint32_t>(m_colours.size());
}

std::optional<std::uint32_t> TintedTemplateSource::image(std::uint32_t i) const noexcept {
    if (m_block == 0 || i >= count()) {
        return std::nullopt;
    }
    const std::uint32_t in_block = i % m_block;
    for (const UnitImageGroup& g : m_groups) {
        if (in_block >= g.offset && in_block < g.offset + g.stride) {
            return g.first + (in_block - g.offset);
        }
    }
    return std::nullopt;
}

void TintedTemplateSource::size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const {
    width = height = 0;
    if (const auto k = image(i)) {
        m_sheet.size(*k, width, height);
    }
}

bool TintedTemplateSource::pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const {
    const auto k = image(i);
    if (!k || !m_sheet.pixels(*k, rgba)) {
        return false;
    }
    tint_rgba(rgba, m_colours[i / m_block]);
    return true;
}

}  // namespace o1602::render
