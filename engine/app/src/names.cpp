// SPDX-License-Identifier: GPL-3.0-or-later
#include "names.hpp"

#include <array>
#include <cstddef>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr std::array<const char*, sim::kOriginalPlayers> kColourNames = {"player.colour.red", "player.colour.blue",
                                                                         "player.colour.yellow", "player.colour.white"};
// In the order of render::BannerTable::add_more_players.
constexpr std::array<const char*, sim::kMaxPlayers - sim::kOriginalPlayers> kMoreColourNames = {
    "player.colour.orange",  "player.colour.green", "player.colour.teal",   "player.colour.pink",
    "player.colour.magenta", "player.colour.grey",  "player.colour.purple", "player.colour.brown"};

}  // namespace

void name_more_players(sim::World& world) {
    // Only a game that has not run yet: later a person may have taken a slot and its name.
    if (world.time_ms() != 0) {
        return;
    }
    for (std::size_t n = sim::kOriginalPlayers; n < sim::kMaxPlayers; ++n) {
        sim::Player* p = world.player(sim::player_slot(n));
        if (p != nullptr && p->role == sim::PlayerRole::Computer &&
            p->banner == sim::kFirstExtraBanner + (n - sim::kOriginalPlayers) && p->name.empty()) {
            p->name = ui::tr(kMoreColourNames[n - sim::kOriginalPlayers]);
        }
    }
}

std::string colour_player_name(sim::BannerId banner, const std::vector<std::string>& colour_names) {
    if (banner < sim::kOriginalPlayers) {
        return banner < colour_names.size() && !colour_names[banner].empty() ? colour_names[banner]
                                                                             : ui::tr(kColourNames[banner]);
    }
    if (banner >= sim::kFirstExtraBanner && std::size_t{banner} - sim::kFirstExtraBanner < kMoreColourNames.size()) {
        return ui::tr(kMoreColourNames[banner - sim::kFirstExtraBanner]);
    }
    return ui::tr("player.default", {std::to_string(banner + 1)});
}

std::string player_name(const sim::Player& player) {
    return player.name.empty() ? colour_player_name(player.banner, {}) : player.name;
}

std::string city_name(const sim::City& city) {
    return city.name.empty() ? ui::tr("city.default", {std::to_string(city.island + 1), std::to_string(city.index + 1)})
                             : city.name;
}

std::string ship_name(const sim::Ship& ship) {
    return ship.name.empty() ? ui::tr("ship.default", {std::to_string(ship.id)}) : ship.name;
}

}  // namespace o1602::app
