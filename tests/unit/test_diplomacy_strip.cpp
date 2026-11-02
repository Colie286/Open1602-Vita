// SPDX-License-Identifier: GPL-3.0-or-later
// The diplomacy window with more partners than VERTRAG.GAD has rows (diplomacy_strip.hpp): where
// the strip puts the coats of arms, who the partners are and which offers wait for an answer.

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "diplomacy_strip.hpp"

#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;
namespace diplomacy = app::diplomacy;

namespace {

/// Twelve players: the local human (0), a second human (1), ten computers, then the trader, the
/// pirates and the natives.
std::vector<sim::Player> twelve_players() {
    std::vector<sim::Player> p(15);
    p[0].role = sim::PlayerRole::Human;
    p[1].role = sim::PlayerRole::Human;
    for (std::size_t i = 2; i < 12; ++i) {
        p[i].role = sim::PlayerRole::Computer;
    }
    sim::make_neutral(p[12], sim::faction::kTrader);
    sim::make_neutral(p[13], sim::faction::kPirates);
    sim::make_neutral(p[14], sim::faction::kNatives);
    for (std::size_t i = 0; i < p.size(); ++i) {
        p[i].name = "P" + std::to_string(i);
    }
    return p;
}

}  // namespace

TEST_CASE("diplomacy strip: the window keeps the original's rows up to three partners") {
    CHECK(!diplomacy::uses_strip(0));
    CHECK(!diplomacy::uses_strip(3));
    CHECK(diplomacy::uses_strip(4));
    CHECK(diplomacy::uses_strip(11));
    CHECK(diplomacy::uses_strip(3, 2));  // a layout with fewer rows
}

TEST_CASE("diplomacy strip: one row up to six, two rows centred above that, never outside the panel") {
    // Four: one row, centred in the 250 pixels from x = 6.
    CHECK((diplomacy::strip_slot(0, 4) == std::array<std::int32_t, 2>{6 + (250 - 4 * 41) / 2, 378}));
    CHECK((diplomacy::strip_slot(3, 4) == std::array<std::int32_t, 2>{6 + (250 - 4 * 41) / 2 + 3 * 41, 378}));
    // Seven: four on top, three below.
    CHECK((diplomacy::strip_slot(3, 7) == std::array<std::int32_t, 2>{6 + (250 - 4 * 41) / 2 + 3 * 41, 355}));
    CHECK((diplomacy::strip_slot(4, 7) == std::array<std::int32_t, 2>{6 + (250 - 3 * 41) / 2, 402}));
    // Eleven: six and five.
    CHECK((diplomacy::strip_slot(0, 11) == std::array<std::int32_t, 2>{8, 355}));
    CHECK((diplomacy::strip_slot(6, 11) == std::array<std::int32_t, 2>{6 + (250 - 5 * 41) / 2, 402}));
    for (std::size_t count = 4; count <= diplomacy::kMaxPartners; ++count) {
        std::set<std::array<std::int32_t, 2>> seen;
        for (std::size_t i = 0; i < count; ++i) {
            const auto [x, y] = diplomacy::strip_slot(i, count);
            CHECK(x >= diplomacy::kPanelLeft);
            CHECK(x + diplomacy::kFrameWidth <= diplomacy::kPanelLeft + diplomacy::kPanelWidth);
            // Above the treaties, which move down by kTreatiesShift from TRIBUT's 354.
            CHECK(y + diplomacy::kFrameHeight <= 354 + diplomacy::kTreatiesShift + 2);
            seen.insert({x, y});
        }
        CHECK(seen.size() == count);
    }
}

TEST_CASE("diplomacy strip: partners are the other humans and computers, offers those not answered") {
    sim::World world(64, 64);
    world.set_players(twelve_players());
    const std::vector<sim::PlayerId> partners = diplomacy::partners(world, 0);
    REQUIRE(partners.size() == 11);
    CHECK(partners.front() == 1);
    CHECK(partners.back() == 11);

    sim::Diplomacy& d = world.diplomacy();
    CHECK(diplomacy::open_offers(world, 0).empty());
    // Computer 9 offers trade, the second human offers trade and peace, computer 4 got an offer
    // from the local player (not one to answer).
    d.relation(0, 9).trade = sim::Treaty::Received;
    d.relation(0, 1).trade = sim::Treaty::Received;
    d.relation(0, 1).peace = sim::Treaty::Received;
    d.relation(0, 4).trade = sim::Treaty::Offered;
    const std::vector<diplomacy::Offer> offers = diplomacy::open_offers(world, 0);
    REQUIRE(offers.size() == 2);
    CHECK((offers[0] == diplomacy::Offer{1, true, true}));
    CHECK((offers[1] == diplomacy::Offer{9, true, false}));
    // Answered: gone.
    d.relation(0, 9).trade = sim::Treaty::Signed;
    CHECK(diplomacy::open_offers(world, 0).size() == 1);
    // Nobody to answer for a player who is not in the game.
    CHECK(diplomacy::open_offers(world, sim::kNoPlayer).empty());
    // A defeated opponent leaves the window, and its offers with it.
    world.player(1)->status = sim::PlayerStatus::Defeated;
    const std::vector<sim::PlayerId> left = diplomacy::partners(world, 0);
    CHECK(left.size() == 10);
    CHECK(std::find(left.begin(), left.end(), sim::PlayerId{1}) == left.end());
    CHECK(diplomacy::open_offers(world, 0).empty());
}

TEST_CASE("diplomacy strip: a partner's wars") {
    sim::World world(64, 64);
    world.set_players(twelve_players());
    sim::Diplomacy& d = world.diplomacy();
    d.resize(world.players().size());
    CHECK(diplomacy::enemies(world, 3).empty());
    for (const auto& [a, b] : {std::pair<sim::PlayerId, sim::PlayerId>{3, 5}, {3, 0}, {3, 7}}) {
        d.relation(a, b).peace = sim::Treaty::None;
        d.relation(b, a).peace = sim::Treaty::None;
    }
    d.relation(3, 7).peace = sim::Treaty::Offered;  // an offer not answered yet: still war
    world.player(5)->status = sim::PlayerStatus::Defeated;
    CHECK((diplomacy::enemies(world, 3) == std::vector<sim::PlayerId>{0, 7}));
    CHECK((diplomacy::enemies(world, 0) == std::vector<sim::PlayerId>{3}));
    CHECK(diplomacy::wars_text(world, 1).find("at peace with everyone") != std::string::npos);
    CHECK(diplomacy::wars_text(world, 3) == "P3 is at war with P0, P7");
    CHECK(diplomacy::enemies(world, sim::kNoPlayer).empty());
}

TEST_CASE("diplomacy strip: the status bar names who offers what") {
    ui::set_language("en");
    CHECK(diplomacy::offer_text({3, true, false}, "Bertram") == "Bertram offers you a trade treaty");
    CHECK(diplomacy::offer_text({3, false, true}, "Bertram") == "Bertram offers you a peace treaty");
    CHECK(diplomacy::offer_text({3, true, true}, "Bertram") == "Bertram offers you a trade treaty and a peace treaty");
    ui::set_language("de");
    CHECK(diplomacy::offer_text({3, true, false}, "Bertram") == "Bertram bietet Euch einen Handelsvertrag an");
    ui::set_language("en");
}
