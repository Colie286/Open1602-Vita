// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/world_graphics.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "o1602/assets/figure_looks.hpp"
#include "o1602/render/dither.hpp"
#include "o1602/render/flag_tint.hpp"
#include "o1602/render/package_sprites.hpp"
#include "o1602/render/unit_tint.hpp"

namespace o1602::render {

std::string sheet_path(assets::FigureSheet sheet, Zoom zoom) {
    constexpr const char* kDirs[] = {"gfx/", "mgfx/", "sgfx/"};
    return std::string(kDirs[static_cast<int>(zoom) % 3]) + assets::figure_sheet_file(sheet);
}

core::Result<WorldGraphics> load_world_graphics(const assets::GameDirectory& game, const assets::Palette& palette,
                                                Zoom zoom, std::span<const assets::FigureSheet> figure_sheets,
                                                const GraphicsOptions& options, const BannerTable* banners,
                                                const assets::FigureTable* figures) {
    struct Loaded {
        assets::FigureSheet id;
        assets::SpriteSheet sheet;
        bool derived;
    };
    std::vector<Loaded> loaded;
    const auto load = [&](assets::FigureSheet id, Zoom z) -> std::optional<assets::SpriteSheet> {
        const auto path = game.resolve(sheet_path(id, z));
        if (!path) {
            return std::nullopt;
        }
        auto sheet = assets::SpriteSheet::load(*path);
        if (!sheet) {
            return std::nullopt;
        }
        return std::move(sheet).value();
    };

    auto terrain = load(assets::FigureSheet::Stadtfld, zoom);
    if (!terrain) {
        return core::make_error(core::ErrorCode::NotFound,
                                sheet_path(assets::FigureSheet::Stadtfld, zoom) + " not found or invalid");
    }
    loaded.push_back(Loaded{assets::FigureSheet::Stadtfld, std::move(*terrain), false});
    for (const assets::FigureSheet id : figure_sheets) {
        if (id == assets::FigureSheet::Stadtfld) {
            continue;
        }
        if (auto sheet = load(id, zoom)) {
            loaded.push_back(Loaded{id, std::move(*sheet), false});
        } else if (options.derive_missing_sheets && zoom != Zoom::Large) {
            const auto larger = static_cast<Zoom>(static_cast<int>(zoom) - 1);
            if (auto big = load(id, larger)) {
                loaded.push_back(Loaded{id, std::move(*big), true});
            }
        }
    }

    // Sources refer to the loaded sheets, which must not move from here on.
    std::vector<std::unique_ptr<SpriteSource>> sources;
    std::vector<const SpriteSource*> pointers;
    for (const Loaded& l : loaded) {
        if (l.derived) {
            sources.push_back(std::make_unique<HalfSizeSource>(l.sheet, palette));
        } else {
            sources.push_back(std::make_unique<SheetSource>(l.sheet, palette));
        }
        pointers.push_back(sources.back().get());
    }
    // The packages' images join the terrain at their sprite numbers, after STADTFLD's.
    std::vector<std::unique_ptr<PackageImageSource>> package_sources;
    PlacedSources terrain_with_packages;
    if (!options.package_images.empty() && pointers.front()->count() <= assets::kFirstPackageSprite) {
        terrain_with_packages.place(0, *pointers.front());
        for (const assets::PackageImage& image : options.package_images) {
            package_sources.push_back(std::make_unique<PackageImageSource>(image, static_cast<std::size_t>(zoom)));
            if (!package_sources.back()->error().empty()) {
                std::fprintf(stderr, "mods: %s: %s\n", image.id.c_str(), package_sources.back()->error().c_str());
            }
            terrain_with_packages.place(image.first, *package_sources.back());
        }
        pointers.front() = &terrain_with_packages;
    }
    std::optional<RedirectedSprites> redirected;
    if (!options.sprite_redirects.empty() && pointers.front() == &terrain_with_packages) {
        redirected.emplace(terrain_with_packages, options.sprite_redirects);
        pointers.front() = &*redirected;
    }
    // Tinted player flags after all sheets: one block of frames per banner with a tinted flag.
    std::vector<std::optional<std::uint32_t>> flag_slot;  // block index per banner
    std::size_t flag_source = pointers.size();
    if (banners != nullptr && banners->flag_template()) {
        const SpriteSource* ship = nullptr;
        for (std::size_t i = 0; i < loaded.size(); ++i) {
            if (loaded[i].id == assets::FigureSheet::Ship) {
                ship = sources[i].get();
            }
        }
        std::vector<assets::Rgba> colours;
        for (const Banner& b : banners->banners()) {
            if (b.flag == FlagStyle::Tinted && ship != nullptr) {
                flag_slot.emplace_back(static_cast<std::uint32_t>(colours.size()));
                colours.push_back(b.colour);
            } else {
                flag_slot.emplace_back(std::nullopt);
            }
        }
        if (!colours.empty()) {
            const FlagTemplate& t = *banners->flag_template();
            // A bad mod would throw out_of_range while the atlas is built (HI-13): say which.
            if (std::uint64_t{t.gfx} + t.frames > ship->count()) {
                return core::make_error(core::ErrorCode::InvalidFormat,
                                        "the flag sprites " + std::to_string(t.gfx) + " + " + std::to_string(t.frames) +
                                            " lie beyond the " + std::to_string(ship->count()) + " ship sprites");
            }
            sources.push_back(std::make_unique<TintedFlagSource>(*ship, t.gfx, t.frames, colours, t.from_original));
            pointers.push_back(sources.back().get());
        }
    }
    // Tinted units after that: one block per banner whose units have no drawn variant.
    std::vector<std::optional<std::uint32_t>> unit_slot;
    std::size_t unit_source = pointers.size();
    std::vector<UnitImageGroup> unit_groups;
    if (banners != nullptr && figures != nullptr) {
        const SpriteSource* soldat = nullptr;
        for (std::size_t i = 0; i < loaded.size(); ++i) {
            if (loaded[i].id == assets::FigureSheet::Soldat) {
                soldat = sources[i].get();
            }
        }
        for (const std::uint32_t family : assets::FigureTable::kUnitFamilies) {
            const assets::FigureType* first = figures->get(family);
            const assets::FigureType* second = figures->get(family + 1);
            if (first == nullptr || second == nullptr || second->gfx <= first->gfx) {
                continue;
            }
            constexpr std::int32_t kFigtypKavalerie = 2;  // figuren.cod Kind FIGTYP_KAVALERIE
            UnitImageGroup g;
            g.first = first->gfx;
            g.stride = second->gfx - first->gfx;
            g.variants = assets::FigureTable::kUnitVariants;
            g.base = assets::FigureTable::kUnitVariants - 1;  // white
            g.mounted = first->kind == kFigtypKavalerie;
            g.family = family;
            unit_groups.push_back(g);
        }
        std::vector<UnitTintColour> colours;
        for (const Banner& b : banners->banners()) {
            if (soldat != nullptr && !unit_groups.empty() && !b.unit_variant && b.flag == FlagStyle::Tinted) {
                unit_slot.emplace_back(static_cast<std::uint32_t>(colours.size()));
                colours.push_back(UnitTintColour{b.colour, b.unit_mount});
            } else {
                unit_slot.emplace_back(std::nullopt);
            }
        }
        if (!colours.empty()) {
            // A bad mod would throw out_of_range while the atlas is built (HI-13): say which figure.
            for (const UnitImageGroup& g : unit_groups) {
                if (std::uint64_t{g.first} + std::uint64_t{g.stride} * g.variants > soldat->count()) {
                    return core::make_error(core::ErrorCode::InvalidFormat,
                                            "figure " + std::to_string(g.family) + ": its " +
                                                std::to_string(g.variants) + " colours from sprite " +
                                                std::to_string(g.first) + " lie beyond the " +
                                                std::to_string(soldat->count()) + " soldier sprites");
                }
            }
            // Player colours of the drawn variants: the original red, blue, yellow and white.
            std::vector<assets::Rgba> variant_colours(
                BannerTable::kDefaultColours.begin(),
                BannerTable::kDefaultColours.begin() + assets::FigureTable::kUnitVariants);
            auto source = std::make_unique<TintedUnitSource>(*soldat, unit_groups, variant_colours, colours);
            std::uint32_t offset = 0;
            for (UnitImageGroup& g : unit_groups) {  // same layout as in the source
                g.offset = offset;
                offset += g.stride;
            }
            sources.push_back(std::move(source));
            pointers.push_back(sources.back().get());
        }
    }
    // Package units on the terrain sheet: drawn as tint templates for every banner, or with a
    // block per colour, whose template is made like the original's for banners without a variant.
    const SpriteSource& terrain_source = *pointers.front();
    const auto in_terrain = [&](std::uint64_t first, std::uint64_t n) { return first + n <= terrain_source.count(); };
    std::vector<UnitImageGroup> template_groups;
    for (const auto& [first, n] : options.template_sprites) {
        if (n == 0 || !in_terrain(first, n)) {
            continue;  // checked by the packages' reader; a bad range is left out
        }
        const bool known = std::any_of(template_groups.begin(), template_groups.end(),
                                       [&](const UnitImageGroup& g) { return g.first == first; });
        if (!known) {
            UnitImageGroup g;
            g.first = first;
            g.stride = n;
            g.variants = 1;
            template_groups.push_back(g);
        }
    }
    const auto is_template = [&](std::uint32_t sprite) {
        return std::any_of(template_groups.begin(), template_groups.end(),
                           [&](const UnitImageGroup& g) { return sprite >= g.first && sprite < g.first + g.stride; });
    };
    std::size_t template_source = pointers.size();
    if (banners != nullptr && !template_groups.empty()) {
        std::vector<assets::Rgba> colours;
        for (const Banner& b : banners->banners()) {
            colours.push_back(b.colour);
        }
        sources.push_back(std::make_unique<TintedTemplateSource>(terrain_source, template_groups, colours));
        pointers.push_back(sources.back().get());
        std::uint32_t offset = 0;
        for (UnitImageGroup& g : template_groups) {  // same layout as in the source
            g.offset = offset;
            offset += g.stride;
        }
    }
    std::vector<UnitImageGroup> package_groups;
    std::size_t package_source = pointers.size();
    if (banners != nullptr && figures != nullptr) {
        for (const std::uint32_t family : figures->unit_families()) {
            if (std::find(assets::FigureTable::kUnitFamilies.begin(), assets::FigureTable::kUnitFamilies.end(),
                          family) != assets::FigureTable::kUnitFamilies.end()) {
                continue;
            }
            const assets::FigureType* first = figures->get(family);
            const assets::FigureType* second = figures->get(family + 1);
            if (first == nullptr || second == nullptr || !assets::draws_package_image(*first) ||
                !assets::draws_package_image(*second) || second->gfx <= first->gfx || is_template(first->gfx)) {
                continue;  // one image for all colours: it stays as drawn
            }
            constexpr std::int32_t kFigtypKavalerie = 2;
            UnitImageGroup g;
            g.first = first->gfx;
            g.stride = second->gfx - first->gfx;
            g.variants = assets::FigureTable::kUnitVariants;
            g.base = assets::FigureTable::kUnitVariants - 1;
            g.mounted = first->kind == kFigtypKavalerie;
            g.family = family;
            if (in_terrain(g.first, std::uint64_t{g.stride} * g.variants)) {
                package_groups.push_back(g);
            }
        }
        std::vector<UnitTintColour> colours;
        for (const Banner& b : banners->banners()) {
            if (!package_groups.empty() && !b.unit_variant && b.flag == FlagStyle::Tinted) {
                colours.push_back(UnitTintColour{b.colour, b.unit_mount});
            }
        }
        if (!colours.empty()) {
            std::vector<assets::Rgba> variant_colours(
                BannerTable::kDefaultColours.begin(),
                BannerTable::kDefaultColours.begin() + assets::FigureTable::kUnitVariants);
            sources.push_back(
                std::make_unique<TintedUnitSource>(terrain_source, package_groups, variant_colours, colours));
            pointers.push_back(sources.back().get());
            std::uint32_t offset = 0;
            for (UnitImageGroup& g : package_groups) {
                g.offset = offset;
                offset += g.stride;
            }
        }
    }
    // Dithered shadows and smoke last (classic graphics): two blocks per sheet.
    std::vector<std::pair<std::size_t, std::size_t>> dithered;  // loaded sheet, source
    for (std::size_t i = 0; i < loaded.size() && options.dither; ++i) {
        if (loaded[i].id == assets::FigureSheet::Schatten || loaded[i].id == assets::FigureSheet::Effekte) {
            dithered.emplace_back(i, pointers.size());
            sources.push_back(std::make_unique<DitheredSource>(*sources[i]));
            pointers.push_back(sources.back().get());
        }
    }
    auto atlas = SpriteAtlas::build(std::span<const SpriteSource* const>(pointers), options.page_size, options.cancel);
    if (!atlas) {
        return atlas.error();
    }
    WorldGraphics g{std::move(atlas).value(), {}, {}, {}, {}, {}, {}, {}};
    for (std::size_t i = 0; i < loaded.size(); ++i) {
        const auto k = static_cast<std::size_t>(loaded[i].id);
        g.base[k] = g.atlas.sheet_base(i);
        g.derived[k] = loaded[i].derived;
    }
    if (flag_source < pointers.size()) {
        const std::uint32_t base = g.atlas.sheet_base(flag_source);
        const std::uint32_t frames = banners->flag_template()->frames;
        for (const auto& slot : flag_slot) {
            g.banner_flags.push_back(slot ? std::optional<std::uint32_t>(base + *slot * frames) : std::nullopt);
        }
    }
    if (unit_source < pointers.size()) {
        const std::uint32_t base = g.atlas.sheet_base(unit_source);
        g.unit_tint.groups = unit_groups;
        for (const UnitImageGroup& group : unit_groups) {
            g.unit_tint.block_size += group.stride;
        }
        for (const auto& slot : unit_slot) {
            g.unit_tint.banner_base.push_back(slot ? std::optional<std::uint32_t>(base + *slot * g.unit_tint.block_size)
                                                   : std::nullopt);
        }
    }
    if (package_source < pointers.size()) {
        const std::uint32_t base = g.atlas.sheet_base(package_source);
        g.package_unit_tint.groups = package_groups;
        for (const UnitImageGroup& group : package_groups) {
            g.package_unit_tint.block_size += group.stride;
        }
        std::uint32_t slot = 0;
        for (const Banner& b : banners->banners()) {
            if (!b.unit_variant && b.flag == FlagStyle::Tinted) {
                g.package_unit_tint.banner_base.emplace_back(base + slot++ * g.package_unit_tint.block_size);
            } else {
                g.package_unit_tint.banner_base.emplace_back(std::nullopt);
            }
        }
    }
    if (template_source < pointers.size() && !template_groups.empty()) {
        const std::uint32_t base = g.atlas.sheet_base(template_source);
        g.template_tint.groups = template_groups;
        for (const UnitImageGroup& group : template_groups) {
            g.template_tint.block_size += group.stride;
        }
        for (std::size_t b = 0; b < banners->banners().size(); ++b) {
            g.template_tint.banner_base.emplace_back(base + static_cast<std::uint32_t>(b) * g.template_tint.block_size);
        }
    }
    for (const auto& [sheet, source] : dithered) {
        g.dither.sheets.push_back(
            DitheredSheet{g.atlas.sheet_base(sheet), pointers[sheet]->count(), g.atlas.sheet_base(source)});
    }
    return g;
}

core::Result<PreparedGraphics> prepare_world_graphics(const assets::GameDirectory& game, const assets::Palette& palette,
                                                      Zoom zoom, std::span<const assets::FigureSheet> figure_sheets,
                                                      const GraphicsOptions& options, const BannerTable* banners,
                                                      const assets::FigureTable* figures) {
    const auto start = std::chrono::steady_clock::now();
    auto graphics = load_world_graphics(game, palette, zoom, figure_sheets, options, banners, figures);
    if (!graphics) {
        return graphics.error();
    }
    PreparedGraphics p{std::move(graphics).value(), {}, 0};
    if (options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed)) {
        return core::make_error(core::ErrorCode::Unsupported, "cancelled");
    }
    // Buildings and units are picked by their visible pixels: every sprite keeps a mask, the
    // terrain sheet's (buildings, also those of mods) as the figures'.
    p.hit_mask = SpriteHitMask::build(p.graphics.atlas, 0);
    p.ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    return p;
}

}  // namespace o1602::render
