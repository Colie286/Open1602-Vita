// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// What the packages change of a new game (ADR 0007, docs/modding/open-work.md): their files read in
// the order the game reads them, each problem as a line. The game logs the problems; o1602-tool
// check-mod lists them.

#include <span>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_looks.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/own_figures.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/package_sounds.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/audio/package_music.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/faction.hpp"
#include "o1602/sim/package_landscapes.hpp"

namespace o1602::app {

/// What a new game starts with as the mods change it.
struct ModRules {
    std::vector<sim::Faction> factions = sim::base_factions();
    /// Factions of packages that take the slots of others in new games (factions.toml `replaces`).
    std::vector<sim::FactionReplacement> faction_replacements;
    sim::Balance balance;
    /// The buildings the packages added, with the Ids `buildings` gave them.
    std::vector<assets::OwnBuilding> own;
    /// The figures the packages added, with the numbers `figures` gave them.
    std::vector<assets::OwnFigure> own_figures;
    /// The unit families among them, which castles train (own-figures.md#soldiers).
    std::vector<assets::OwnUnit> own_units;
    /// The ships of the packages, which shipyards build beside the original's.
    std::vector<assets::OwnShip> own_ships;
    /// The original's goods and those the packages added (goods.toml).
    assets::Goods goods;
    /// The classes of inhabitants as the packages change and add them (classes.toml); empty when
    /// none does.
    std::vector<assets::ResidentClass> classes;
    /// The unlock levels as the packages change and add them (levels.toml); empty when none does.
    std::vector<assets::InfraLevel> levels;
    /// The images of the packages with their sprite numbers (graphics.toml).
    std::vector<assets::PackageImage> images;
    /// The figures the packages draw with their images (figure_images.toml).
    std::vector<assets::FigureLook> looks;
    /// The music of the packages (music.toml).
    std::vector<audio::MusicTrack> music;
    /// The sounds of the packages with their sound numbers (sounds.toml).
    std::vector<assets::PackageSound> sounds;
};

/// The icons of `packages` (icons.toml), numbered in their load order, after Open1602's own.
[[nodiscard]] std::vector<assets::PackageIcon> load_package_icons(std::span<const assets::ModPackage> packages,
                                                                  std::vector<std::string>& problems);

/// The kinds of island world of `packages` (landscapes.toml), in their load order.
[[nodiscard]] std::vector<sim::PackageLandscape> load_package_landscapes(std::span<const assets::ModPackage> packages,
                                                                         std::vector<std::string>& problems);

/// The base game's factions and numbers as the mods change them (<package folder>/factions.toml and
/// balance.toml), the packages in their load order; the buildings they add go into `buildings`
/// first (own_buildings.toml), so that a package can change another package's building too.
[[nodiscard]] ModRules load_mod_rules(std::span<const assets::ModPackage> packages, assets::BuildingTable& buildings,
                                      assets::FigureTable& figures, std::vector<std::string>& problems);

}  // namespace o1602::app
