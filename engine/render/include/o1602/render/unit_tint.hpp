// SPDX-License-Identifier: GPL-3.0-or-later
//
// Units in any player colour. The original draws soldiers, riders, musketeers and gunners four
// times, once per player colour (red, blue, yellow, white). Banners with one of these colours use
// the drawn images; every other banner gets a template made from the four variants and tinted
// like the generic flag (flag_tint.hpp). The variants are drawn separately, not recoloured, so a
// tinted unit is similar to the originals but not identical (docs/re/figures.md).
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "o1602/assets/palette.hpp"
#include "o1602/render/sprite_source.hpp"

namespace o1602::render {

/// The images of one unit family in its sheet: variant v of image k is `first + v * stride + k`.
struct UnitImageGroup {
    std::uint32_t first = 0;    ///< image 0 of variant 0
    std::uint32_t stride = 0;   ///< images per variant
    std::uint8_t variants = 0;  ///< 4 in the original
    std::uint8_t base = 0;      ///< variant the cloth shading comes from (3 = white in the original)
    std::uint32_t offset = 0;   ///< position of the family in a tinted block
    /// Riders: the horse is taken from one of the drawn variants (UnitTintColour::mount), the rest
    /// from the template.
    bool mounted = false;
    std::uint32_t family = 0;  ///< figure number of variant 0, for messages
};

/// Where the tinted units of each banner are in an atlas.
struct UnitTint {
    std::vector<UnitImageGroup> groups;
    std::uint32_t block_size = 0;  ///< images per banner (sum of the groups' strides)
    /// Per banner (BannerTable index): first atlas index of its tinted block, nothing for banners
    /// that use drawn variants or have no tint.
    std::vector<std::optional<std::uint32_t>> banner_base;

    /// Atlas index of image `image` (index in the sheet, of any variant) in the tinted block of
    /// `banner`; nothing if the banner has no block or the image belongs to no group.
    [[nodiscard]] std::optional<std::uint32_t> sprite(std::size_t banner, std::uint32_t image) const noexcept;
};

/// An RGBA8 image.
struct RgbaView {
    const std::uint8_t* pixels = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct UnitTemplateOptions {
    std::size_t base = 3;  ///< variant the cloth shading comes from (white)
    /// Variant the image is built on: its size, and for riders its horse. Variants are aligned at
    /// their bottom centre, the anchor the original draws figures at.
    std::size_t body = 3;
    bool mounted = false;
    /// Factor for the brightness of the horse (1 = as drawn).
    double mount_brightness = 1.0;
};

/// Builds the tint template of one unit image from its colour variants (`out`, `width` x `height`
/// = size of variants[options.body]). `colours[v]` is the player colour of variant v.
///
/// A variant *shows its colour* at a pixel if the pixel differs from the base variant and its hue
/// is within 60 degrees of the variant's player colour and closer to it than to the other
/// variants' colours (brown lies between red and yellow and counts for neither). Cloth is
/// - every pixel where at least two variants show their colour, and
/// - every pixel connected to those where one variant shows its colour and the base variant is
///   white or light grey (the plumes, which the variants draw in different shapes).
/// Cloth gets the grey 0.48 * luminance + 5 of the base variant. For riders, the horse (light and
/// neutral in the white variant, grey in the blue one, brown or dark in red and yellow) is taken
/// from the body variant. All other pure grey pixels are moved by one step in blue so that the tint
/// leaves them alone (template convention, docs/modding/flags.md).
void unit_template(std::span<const RgbaView> variants, std::span<const assets::Rgba> colours,
                   const UnitTemplateOptions& options, std::vector<std::uint8_t>& out, std::uint32_t& width,
                   std::uint32_t& height);

/// Colour of one tinted block.
struct UnitTintColour {
    assets::Rgba colour;
    /// Horse of the riders: 0 brown, 1 grey (the blue riders'), 2 dark brown (the yellow riders'),
    /// 3 white (the white riders'). The red riders' brown horse cannot be used: 112 of the 304
    /// red images are drawn differently (other size and pose), so brown is the dark brown horse
    /// made lighter.
    std::uint8_t mount = 3;
};
/// The former name (LC-20).
using UnitTintColor [[deprecated("use UnitTintColour")]] = UnitTintColour;

/// The unit images of all groups, tinted once per colour: colours.size() blocks of block_size
/// images, in group order.
class TintedUnitSource final : public SpriteSource {
public:
    /// `variant_colours`: player colour of each variant (the original: red, blue, yellow, white).
    TintedUnitSource(const SpriteSource& sheet, std::vector<UnitImageGroup> groups,
                     std::vector<assets::Rgba> variant_colours, std::vector<UnitTintColour> colours);
    [[nodiscard]] std::uint32_t count() const override;
    void size(std::uint32_t i, std::uint32_t& width, std::uint32_t& height) const override;
    bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override;

private:
    /// Group and image k of image i of a block.
    [[nodiscard]] const UnitImageGroup* locate(std::uint32_t i, std::uint32_t& k) const noexcept;
    [[nodiscard]] std::size_t body(const UnitImageGroup& g, std::uint32_t i) const noexcept;
    /// The tint template of image k of a group (unit_template of its variants); false without the
    /// base or body variant.
    bool make_template(const UnitImageGroup& g, std::uint32_t k, const UnitTemplateOptions& options,
                       std::vector<std::uint8_t>& rgba) const;

    const SpriteSource& m_sheet;
    std::vector<UnitImageGroup> m_groups;
    std::vector<assets::Rgba> m_variant_colours;
    std::vector<UnitTintColour> m_colours;
    std::uint32_t m_block = 0;
    /// One template: made by the first thread that asks for it, the others wait for it.
    struct Template {
        std::once_flag made;
        bool ok = false;
        std::vector<std::uint8_t> rgba;
    };
    /// The templates, by image of a block, body variant and horse brightness: they do not depend on
    /// the colour, so each is made once, not once per banner (FB-25).
    mutable std::mutex m_templates_mutex;
    mutable std::unordered_map<std::uint64_t, std::shared_ptr<Template>> m_templates;
    mutable std::size_t m_template_bytes=0;
};

}  // namespace o1602::render
