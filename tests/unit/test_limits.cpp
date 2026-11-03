// SPDX-License-Identifier: GPL-3.0-or-later
//
// Limits that more than one module checks (LC-1, LC-13, LC-14): the relations between them, and that
// a limit reaches the player as one rather than as a damaged file.

#include <array>
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/core/result.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/sim/limits.hpp"
#include "o1602/sim/player.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_plan.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;

TEST_CASE("limits: the modules derive theirs from one place") {
    // A colour of its own for every player with a start (the hand-picked ones, generated beyond
    // them: "banners: players five to twelve ..."), and a slot below "nobody".
    static_assert(sim::player_slot(sim::kMaxPlayers - 1) < sim::kNoPlayer);
    static_assert(sim::kMaxPlayerSlots == sim::kFirstExtraPlayer + sim::kMaxPlayers - sim::kOriginalPlayers);
    // The lobby shows every slot, a roster holds the host and every computer it takes.
    static_assert(net::kMaxLobbySeats >= sim::kMaxPlayerSlots);
    static_assert(net::kMaxMembers + 1 == net::kMaxRosterMembers);
    // Islands: the generator's numbers fit a byte, a world's indexes 16 bits besides the sea.
    static_assert(sim::kMaxPlannedIslands < sim::kMaxIslandNumbers);
    static_assert(sim::kMaxIslands <= sim::World::kSea);
    static_assert(sim::World::kMaxSide == sim::kMaxWorldSide);
    static_assert(sim::WorldSettings::kMaxSide <= sim::kMaxWorldSide);
    static_assert(sim::kMaxCitiesPerIsland < sim::kCitySlotsPerIsland);
    CHECK(sim::kMaxPlayerSlots == sim::kMaxPlayers + 3);  // 15 for twelve
}

TEST_CASE("limits: the world plan takes the players add_players places, not more") {
    sim::WorldSettings settings;
    settings.players = static_cast<std::uint8_t>(sim::kMaxPlayers + 1);
    const auto plan = sim::plan_world(settings, {});
    REQUIRE(!plan.ok());
    CHECK(plan.error().code == core::ErrorCode::LimitExceeded);
    CHECK(plan.error().message.find(std::to_string(sim::kMaxPlayers)) != std::string::npos);

    // At the limit the players pass and the plan fails for the missing templates only.
    settings.players = static_cast<std::uint8_t>(sim::kMaxPlayers);
    const auto at_limit = sim::plan_world(settings, {});
    REQUIRE(!at_limit.ok());
    CHECK(at_limit.error().code == core::ErrorCode::NotFound);

    settings.width = sim::WorldSettings::kMaxSide + 1;
    const auto wide = sim::plan_world(settings, {});
    REQUIRE(!wide.ok());
    CHECK(wide.error().code == core::ErrorCode::LimitExceeded);
}

TEST_CASE("limits: a limit reaches the player as one, not as a damaged file") {
    ui::set_language("en");
    const core::Error limit{core::ErrorCode::LimitExceeded, "1 to 12 players"};
    const std::string text = ui::error_text(limit, "test");
    CHECK(text.find("1 to 12 players") != std::string::npos);
    CHECK(text != ui::error_text(core::Error{core::ErrorCode::InvalidFormat, "bad"}, "test"));
    ui::set_language("de");
    CHECK(ui::error_text(limit, "test").find("Spiel") != std::string::npos);
    ui::set_language("en");
}
