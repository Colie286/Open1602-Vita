// SPDX-License-Identifier: GPL-3.0-or-later
//
// Figures of a package's own, Open1602's own: `own_figures.toml` of a package declares figures that
// the original's `figuren.cod` does not have (docs/modding/own-figures.md). Each one copies a figure
// of the original (`like`), may draw an image of a package, and gets a number of its own in the
// figure table. Its string id `<package>:<name>` is what packages and savegames name it by, so that
// the numbers may differ between installations.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/localized_names.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/package_sounds.hpp"

namespace o1602::assets {

/// The file of a package that declares its figures.
inline constexpr std::string_view kOwnFiguresFile = "own_figures.toml";
/// Figures one package may declare, so that a broken file cannot fill the table.
inline constexpr std::size_t kMaxOwnFigures = 64;

/// A figure a package added, with the number the figure table gave it in this run.
struct OwnFigure {
    std::string id;            ///< "<package>:<name>", UTF-8
    std::uint32_t number = 0;  ///< its `FIGUR` number in this run's figure table

    friend bool operator==(const OwnFigure&, const OwnFigure&) = default;
};

/// A unit family a package added: a figure whose `like` names a unit of the original's families or
/// of another package (docs/modding/own-figures.md#soldiers). Castles train it beside the family it
/// copies: a click on that family's button picks the next one.
struct OwnUnit {
    std::string id;            ///< "<package>:<name>", as its OwnFigure
    std::uint32_t family = 0;  ///< its red figure; blue, yellow and white follow
    std::uint32_t like = 0;    ///< the red figure of the original's family it stands beside
    std::string icon;          ///< an icon of a package (icons.toml) for the castle, empty: the original's
    LocalizedNames names;      ///< Windows-1252
    /// Its image is a tint template (`tint = true`): every pure grey pixel takes on the colour of
    /// the owner's banner, for every banner (docs/modding/flags.md).
    bool tint = false;

    /// The name in `language`, else in English, else the first, else the id.
    [[nodiscard]] std::string name(std::string_view language) const;
};

/// A ship of a package that shipyards build: a figure whose `like` names a ship of the original's
/// shipyards (or another package's), followed by its wreck (docs/modding/own-figures.md#ships).
/// A shipyard builds it beside the original's ship of its type: a click on that type's button
/// again picks the next one.
struct OwnShip {
    std::string id;            ///< "<package>:<name>", as its OwnFigure
    std::uint32_t figure = 0;  ///< its number; the wreck follows
    std::uint8_t type = 0;     ///< the original's ship it stands beside, an index of figure::kShips
    std::string icon;          ///< an icon of a package (icons.toml) for the shipyard, empty: the original's
    LocalizedNames names;      ///< Windows-1252
    /// Its image is a tint template (`tint = true`), in the owner's colour for every banner.
    bool tint = false;

    /// The name in `language`, else in English, else the first, else the id.
    [[nodiscard]] std::string name(std::string_view language) const;
};

/// The ship of `ships` with number `figure`, nullptr for the original's.
[[nodiscard]] const OwnShip* find_own_ship(std::span<const OwnShip> ships, std::uint32_t figure) noexcept;

/// The entry of `own` called `id`, nullptr if there is none.
[[nodiscard]] const OwnFigure* find_own_figure(std::span<const OwnFigure> own, std::string_view id) noexcept;

/// The one figure `id` names: "anno1602:<name or number>" of the original (a unit's name stands for
/// four figures and names none here) or "<package>:<name>" of `own`. Nothing if it names none.
[[nodiscard]] std::optional<std::uint32_t> figure_number(std::span<const OwnFigure> own, const FigureTable& figures,
                                                         std::string_view id);

/// Reads `own_figures.toml` of `package` and adds its figures to `table` and `own`. Packages are
/// read in their load order, so the numbers are the same on every computer with the same packages.
/// A table that cannot be used is passed over with a warning; a file that is no data file adds
/// nothing. `images`: what `image` may name (package_graphics.hpp); `sounds`: what `sound` may name
/// (package_sounds.hpp). A figure whose `like` names a unit family (`anno1602:musketeer`, or a unit
/// of a package) becomes a unit family of four colours (FigureTable::add_unit_family) and is added
/// to `units` too, when given.
void apply_own_figures_file(FigureTable& table, std::vector<OwnFigure>& own, std::string_view package,
                            std::string_view utf8, std::vector<std::string>& warnings,
                            std::span<const PackageImage> images = {}, std::span<const PackageSound> sounds = {},
                            std::vector<OwnUnit>* units = nullptr, std::vector<OwnShip>* ships = nullptr);

/// The red figure `id` names when it names a unit family: "anno1602:<name>" of one of the
/// original's (a unit's name stands for its four colours) or "<package>:<name>" of `own`; whether
/// that figure heads a family of a table is the caller's to check (FigureTable::unit_of).
[[nodiscard]] std::optional<std::uint32_t> unit_family_number(std::span<const OwnFigure> own, std::string_view id);

/// The sprites (first, count) of the terrain sheet that units of `units` and ships of `ships` draw
/// as tint templates.
[[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> tint_template_sprites(
    std::span<const OwnUnit> units, const FigureTable& table, std::span<const OwnShip> ships = {});

/// The unit family of `units` whose red figure is `family`, nullptr for the original's.
[[nodiscard]] const OwnUnit* find_own_unit(std::span<const OwnUnit> units, std::uint32_t family) noexcept;

}  // namespace o1602::assets
