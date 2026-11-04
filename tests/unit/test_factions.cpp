// SPDX-License-Identifier: GPL-3.0-or-later
// Factions as data (ADR 0007, section 3; docs/modding/factions.md): the base game's factions, how
// their numbers grow with the game, a world's own factions and a mod's changes to them.

#include <algorithm>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/faction.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

TEST_CASE("factions: the base game's four, by id and behaviour") {
    const std::vector<sim::Faction> base = sim::base_factions();
    REQUIRE(base.size() == 4);
    CHECK(base[sim::faction::kSettlers].id == "anno1602:settlers");
    CHECK(base[sim::faction::kTrader].id == "anno1602:trader");
    CHECK(base[sim::faction::kPirates].id == "anno1602:pirates");
    CHECK(base[sim::faction::kNatives].id == "anno1602:natives");
    for (std::size_t i = 0; i < base.size(); ++i) {
        CHECK(sim::base_faction(base[i].behaviour) == i);
    }
    CHECK(sim::World(10, 10).factions() == base);
}

TEST_CASE("factions: the default numbers are the original's and Open1602's rules") {
    const sim::Faction f = sim::base_factions()[sim::faction::kPirates];
    const sim::Faction trader = sim::base_factions()[sim::faction::kTrader];
    // One nest for every four human and computer players begun, at least one.
    for (std::uint32_t players = 0; players <= 13; ++players) {
        CHECK(f.nests.of(players) == std::max<std::uint32_t>(1, (players + 3) / 4));
    }
    // One trader for every three Kontors begun, none with fewer than two.
    for (std::uint32_t kontors = 0; kontors <= 13; ++kontors) {
        CHECK(trader.ships.of(kontors) == (kontors < 2 ? 0U : (kontors + 2) / 3));
    }
    CHECK(trader.reach_players == 4);
    const sim::Scaling fixed{3, 0, 0, 0, 5};
    CHECK(fixed.of(0) == 3);
    CHECK(fixed.of(100) == 3);
    const sim::Scaling capped{1, 2, 0, 0, 4};
    CHECK(capped.of(1) == 2);
    CHECK(capped.of(100) == 4);
}

TEST_CASE("factions: a faction of a package acts as the behaviour it names") {
    sim::World world(50, 50);
    std::vector<sim::Faction> factions = sim::base_factions();
    sim::Faction corsairs;
    corsairs.id = "corsairs:corsairs";
    corsairs.behaviour = sim::FactionBehaviour::Pirates;
    factions.push_back(corsairs);
    world.set_factions(factions);
    std::vector<sim::Player> players(3);
    players[1].role = sim::PlayerRole::Computer;
    sim::make_neutral(players[2], 4);
    world.set_players(players);
    CHECK(world.faction(world.players()[2]).id == "corsairs:corsairs");
    CHECK(world.first_of(sim::FactionBehaviour::Pirates) == 2);
    CHECK(world.acts_as(2, sim::FactionBehaviour::Pirates));
    CHECK(!world.acts_as(1, sim::FactionBehaviour::Pirates));
    CHECK(!world.acts_as(sim::kNoPlayer, sim::FactionBehaviour::Settlers));
    // At war with the settlers from the start, as the original's pirates.
    CHECK(world.diplomacy().relation(0, 2).peace == sim::Treaty::None);
    CHECK(world.diplomacy().relation(0, 1).peace == sim::Treaty::Signed);

    // Without the faction the slot is a settler's again.
    world.set_factions(sim::base_factions());
    CHECK(world.players()[2].faction == sim::faction::kSettlers);
    // An empty list is the base game's.
    world.set_factions({});
    CHECK(world.factions() == sim::base_factions());
}

TEST_CASE("factions: a package changes the base game's factions field by field") {
    std::vector<sim::Faction> factions = sim::base_factions();
    std::vector<std::string> warnings;
    sim::apply_faction_file(factions, "bigworld",
                            "# More pirates and traders for large games.\n"
                            "[\"anno1602:pirates\"]\n"
                            "nests.per = 3\n"
                            "nests.max = 5\n"
                            "\n"
                            "[\"anno1602:trader\"]\n"
                            "ships.per = 2\n"
                            "reach_players = 6\n"
                            "colour = 3\n"
                            "ships.from = -1\n"
                            "\n"
                            "[\"anno1602:vikings\"]\n"
                            "nests.per = 1\n"
                            "[corsairs]\n"
                            "nests.per = 1\n",
                            warnings);
    CHECK(factions[sim::faction::kPirates].nests.per == 3);
    CHECK(factions[sim::faction::kPirates].nests.max == 5);
    CHECK(factions[sim::faction::kPirates].nests.of(30) == 5);
    CHECK(factions[sim::faction::kTrader].ships.per == 2);
    CHECK(factions[sim::faction::kTrader].ships.from == 2);  // the wrong value is passed over
    CHECK(factions[sim::faction::kTrader].reach_players == 6);
    CHECK(factions.size() == 4);
    REQUIRE(warnings.size() == 4);
    CHECK(warnings[0] == "bigworld/factions.toml line 9: unknown field 'colour'");
    CHECK(warnings[1] == "bigworld/factions.toml line 10: 'ships.from' must be a number from 0 to 65535");
    CHECK(warnings[2].find("there is no faction anno1602:vikings") != std::string::npos);
    CHECK(warnings[3].find("the new faction bigworld:corsairs needs 'behaviour'") != std::string::npos);

    // A file that is no data file changes nothing.
    warnings.clear();
    const std::vector<sim::Faction> before = factions;
    sim::apply_faction_file(factions, "broken", "[\"anno1602:pirates\"]\nnests.per = \"3\n", warnings);
    CHECK(factions == before);
    CHECK(warnings.size() == 1);
}

TEST_CASE("factions: a package adds factions of its own that act as a base game's and may take its slots") {
    std::vector<sim::Faction> factions = sim::base_factions();
    std::vector<std::string> warnings;
    std::vector<sim::FactionReplacement> replacements;
    sim::apply_faction_file(factions, "seas",
                            "[corsairs]\n"
                            "behaviour = \"pirates\"\n"
                            "replaces = \"anno1602:pirates\"\n"
                            "nests.per = 2\n"
                            "[\"seas:guild\"]\n"
                            "behaviour = \"trader\"\n"
                            "replaces = \"anno1602:pirates\"\n"
                            "[tribe]\n"
                            "behaviour = \"elves\"\n"
                            "[\"other:navy\"]\n"
                            "behaviour = \"settlers\"\n",
                            warnings, &replacements);
    REQUIRE(factions.size() == 6);
    CHECK(factions[4].id == "seas:corsairs");
    CHECK(factions[4].behaviour == sim::FactionBehaviour::Pirates);
    CHECK(factions[4].nests.per == 2);
    CHECK(factions[4].ships == sim::base_factions()[sim::faction::kPirates].ships);
    CHECK(factions[5].id == "seas:guild");
    CHECK(factions[5].behaviour == sim::FactionBehaviour::Trader);
    REQUIRE(replacements.size() == 1);
    CHECK(replacements[0] == (sim::FactionReplacement{"anno1602:pirates", "seas:corsairs"}));
    // The guild acts otherwise than the pirates, the tribe names no behaviour, the navy no faction
    // of the package.
    REQUIRE(warnings.size() == 3);
    CHECK(warnings[0].find("another behaviour") != std::string::npos);
    CHECK(warnings[1].find("needs 'behaviour'") != std::string::npos);
    CHECK(warnings[2].find("only under its own name") != std::string::npos);
    CHECK(sim::find_faction(factions, "seas:guild") == sim::FactionId{5});
    CHECK(!sim::find_faction(factions, "seas:tribe"));

    // In a game the corsairs take the pirates' slots and are saved with it.
    sim::World world(20, 20);
    std::vector<sim::Player> players(3);
    players[0].role = sim::PlayerRole::Human;
    sim::make_neutral(players[1], sim::faction::kPirates);
    sim::make_neutral(players[2], sim::faction::kNatives);
    world.set_players(players);
    world.set_factions(factions);
    world.replace_factions(replacements);
    CHECK(world.players()[1].faction == 4);
    CHECK(world.behaviour(world.players()[1]) == sim::FactionBehaviour::Pirates);
    CHECK(world.players()[2].faction == sim::faction::kNatives);
    CHECK(world.first_of(sim::FactionBehaviour::Pirates) == 1);
}

TEST_CASE("factions: 'scripted' gives settlers or pirates a behaviour of their scripts") {
    std::vector<sim::Faction> factions = sim::base_factions();
    std::vector<std::string> warnings;
    sim::apply_faction_file(factions, "seas",
                            "[corsairs]\n"
                            "behaviour = \"pirates\"\n"
                            "scripted = true\n"
                            "[colonists]\n"
                            "behaviour = \"settlers\"\n"
                            "scripted = true\n"
                            "[\"anno1602:trader\"]\n"
                            "scripted = true\n"  // line 8: the game steers the trader
                            "[\"anno1602:natives\"]\n"
                            "scripted = 1\n",  // line 10: no true or false
                            warnings);
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].starts_with("seas/factions.toml line 8: 'scripted' is for factions of settlers or pirates"));
    CHECK(warnings[1].starts_with("seas/factions.toml line 10: 'scripted' must be true or false"));
    REQUIRE(factions.size() == 6);
    CHECK(factions[4].id == "seas:corsairs");
    CHECK(factions[4].scripted);
    CHECK(factions[5].scripted);
    CHECK(!factions[sim::faction::kTrader].scripted);
    CHECK(!factions[sim::faction::kPirates].scripted);
}

TEST_CASE("factions: a package names its faction by language for the editor") {
    std::vector<sim::Faction> factions = sim::base_factions();
    std::vector<std::string> warnings;
    sim::apply_faction_file(factions, "seas",
                            "[league]\n"
                            "behaviour = \"settlers\"\n"
                            "name.en = \"Hanse League\"\n"
                            "name.de = \"St\xC3\xA4"
                            "dtebund\"\n"
                            "name.fr = 3\n",  // line 5: no text
                            warnings);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].starts_with("seas/factions.toml line 5: 'name.fr' must be name.<language>"));
    REQUIRE(factions.size() == 5);
    const auto& names = factions[4].names;
    REQUIRE(names.size() == 2);
    CHECK((names[0].first == "en" && names[0].second == "Hanse League"));
    CHECK((names[1].first == "de" && names[1].second == "St\xE4"
                                                        "dtebund"));  // Windows-1252 for the fonts
}
