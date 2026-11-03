// SPDX-License-Identifier: GPL-3.0-or-later
//
// Climates of a content package's own (item 9 of docs/modding/open-work.md; docs/modding/climates.md):
// `climates.toml` names climates that are like the original's north or south and differ in their
// name, the crops their islands grow, the trees of their forest and how many islands of a new own
// world take them. An island of such a climate keeps the original's (`like`) in its header, so
// everything the original decides by the climate (templates, animals, palms) stays as it is; the
// island's own climate is WorldIsland::climate. A game keeps the climates it started with (section
// CLIM), so a savegame needs no package for them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace o1602::assets {
class BuildingTable;
struct OwnBuilding;
}  // namespace o1602::assets

namespace o1602::sim {

/// The file of a package with its climates.
inline constexpr std::string_view kClimatesFile = "climates.toml";
/// Climates of all packages together; WorldIsland::climate numbers them from 1.
inline constexpr std::size_t kMaxClimates = 32;
/// Trees one climate's forest may name.
inline constexpr std::size_t kMaxForestTrees = 16;
/// Kinds of ground one climate may name.
inline constexpr std::size_t kMaxGroundTiles = 16;
/// Kinds of island world one climate may give a share of their own.
inline constexpr std::size_t kMaxLandscapeShares = 32;

struct Climate {
    std::string id;         ///< "<package>:<name>"
    std::uint8_t like = 0;  ///< climate::kNorth or climate::kSouth
    /// Its name, Windows-1252, by language code ("" for every language).
    std::map<std::string, std::string, std::less<>> names;
    /// The crops its islands may grow, as bits of IslandHeader::fertility: each grows half on every
    /// island of the climate and fully where the island's bit says so (island_fertility).
    std::uint32_t crops = 0;
    /// The trees of the forest of its islands of their own shape (building Ids); none: the like's.
    std::vector<std::uint32_t> forest;
    /// The ground of its islands of their own shape (building Ids, kind::kGround), each as often;
    /// none: the original's mix (ids::kGroundMix).
    std::vector<std::uint32_t> ground;
    /// Percent of the islands of `like` in a new own world that take this climate (not the homes).
    std::uint8_t share = 0;
    /// The percent in a world of a kind of island world (WorldSettings::Landscape::name, such as
    /// "archipelago" or "<package>:<name>") instead of `share`, by its name.
    std::map<std::string, std::uint8_t, std::less<>> landscape_shares;

    /// The percent of the islands of `like` it takes in a world of the kind `landscape`
    /// (landscape_name(); "" for the original's islands): its share there, else `share`.
    [[nodiscard]] std::uint8_t share_in(std::string_view landscape) const;
    /// The name in `language`, else the one for every language, else English, else the id.
    [[nodiscard]] std::string name(std::string_view language) const;
    friend bool operator==(const Climate&, const Climate&) = default;
};

/// The crops of the original's climate `like` as fertility bits: tobacco, sugar cane and wine in
/// the north, spices, cotton and cocoa in the south (the crops that grow half without the bit).
[[nodiscard]] std::uint32_t original_climate_crops(std::uint8_t like) noexcept;

/// The fertility bit of a crop of a climate by its name in climates.toml ("tobacco", "spices",
/// "sugar", "cotton", "wine", "cocoa"); nothing for another name.
[[nodiscard]] std::optional<std::uint32_t> crop_bit(std::string_view name) noexcept;

/// Reads `climates.toml` of `package` (UTF-8) and adds its climates to `climates`, at most
/// kMaxClimates in all. A field that is wrong is passed over with a warning that names the line;
/// a table without `like` is passed over. `forest` and `ground` name the original's buildings and
/// those of `own`; with `buildings`, only those that are forest (kind::kForest) or ground
/// (kind::kGround).
void apply_climates_file(std::vector<Climate>& climates, std::string_view package, std::string_view utf8,
                         std::vector<std::string>& warnings, const assets::BuildingTable* buildings = nullptr,
                         std::span<const assets::OwnBuilding> own = {});

/// Whether `climates` can be played: at most kMaxClimates, ids once each, `like` north or south,
/// crops of the six only, shares up to 100, at most kMaxForestTrees trees, kMaxGroundTiles
/// kinds of ground and kMaxLandscapeShares shares of kinds of island world.
[[nodiscard]] bool valid(std::span<const Climate> climates) noexcept;

}  // namespace o1602::sim
