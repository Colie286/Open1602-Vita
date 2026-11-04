// SPDX-License-Identifier: GPL-3.0-or-later
// The start dialog of the endless game (ADR 0008): its items and values, the presets, the lines
// in settings.cfg and the screen at work.

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "app_settings.hpp"
#include "endless_setup.hpp"
#include "game_settings_menu.hpp"

#include "o1602/ui/localization.hpp"

using namespace o1602;
using app::EndlessSetup;
using app::SetupItem;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};

std::optional<std::pair<std::int32_t, std::int32_t>> find_text(app::GameSettingsMenu& menu, const std::string& text) {
    for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text == text) {
            return std::pair{d.x, d.y};
        }
    }
    return std::nullopt;
}

app::GameSettingsMenu::Result click(app::GameSettingsMenu& menu, const std::string& text,
                                    ui::MouseButton button = ui::MouseButton::Left) {
    const auto at = find_text(menu, text);
    REQUIRE(at.has_value());
    (void)menu.mouse(at->first, at->second - 4, true, button);
    return menu.mouse(at->first, at->second - 4, false, button);
}

std::string value_of(const EndlessSetup& s, SetupItem item) {
    return app::setup_value(item, app::setup_choice(s, item));
}

}  // namespace

TEST_CASE("endless setup: every item goes round its values and back") {
    for (std::size_t i = 0; i < app::kSetupItems; ++i) {
        const auto item = static_cast<SetupItem>(i);
        EndlessSetup s;
        const std::size_t n = app::setup_choices(item);
        REQUIRE(n >= 2);
        const std::size_t start = app::setup_choice(s, item);
        std::vector<std::string> seen;
        for (std::size_t k = 0; k < n; ++k) {
            seen.push_back(value_of(s, item));
            app::choose_next(s, item);
        }
        CHECK(app::setup_choice(s, item) == start);
        // Backwards: the same values in the other order, before the first the last.
        for (std::size_t k = n; k > 0; --k) {
            app::choose_previous(s, item);
            CHECK(value_of(s, item) == seen[k - 1]);
        }
        CHECK(app::setup_choice(s, item) == start);
        for (std::size_t a = 0; a < seen.size(); ++a) {
            CHECK(!seen[a].empty());
            for (std::size_t b = a + 1; b < seen.size(); ++b) {
                CHECK(seen[a] != seen[b]);  // each value has a text of its own
            }
        }
        CHECK(!app::setup_label(item).empty());
    }
}

TEST_CASE("endless setup: the values reach the rules and the world") {
    EndlessSetup s;
    app::choose(s, SetupItem::Refund, 2);
    CHECK(s.rules.demolish_refund == sim::Refund::Partial);
    CHECK(s.rules.refund_share() == 50);
    app::choose(s, SetupItem::Refund, 4);
    CHECK(s.rules.refund_share() == 100);
    app::choose(s, SetupItem::Ships, 3);
    CHECK(s.rules.max_ships_per_player == 0);  // unlimited
    app::choose(s, SetupItem::Droughts, 4);
    CHECK(s.rules.droughts == sim::Droughts::Frequent);
    app::choose(s, SetupItem::Volcanoes, 1);
    CHECK(s.rules.volcanoes == sim::Volcanoes::Off);
    app::choose(s, SetupItem::TraderPrices, 1);
    CHECK(s.rules.trader_prices == sim::TraderPrices::Market);
    app::choose(s, SetupItem::Resources, 1);
    CHECK(!s.rules.guarantee_resources);
    // The island resources: by ship as in the original by default, on their own beside every raw
    // material, so that a fully explored world goes with any of the other settings.
    CHECK(!s.rules.reveal_resources);
    CHECK(value_of(s, SetupItem::RevealResources) == ui::tr("setup.reveal_resources.original"));
    app::choose(s, SetupItem::RevealResources, 1);
    CHECK(s.rules.reveal_resources);
    CHECK(!s.rules.guarantee_resources);
    CHECK(value_of(s, SetupItem::RevealResources) == ui::tr("setup.reveal_resources.all"));
    CHECK(app::write_setup(s).find("endless.reveal_resources = all\n") != std::string::npos);
    {
        EndlessSetup back;
        app::read_setup(back, app::write_setup(s));
        CHECK(back.rules.reveal_resources);
        CHECK(app::read_setup_line(back, "reveal_resources", "original"));
        CHECK(!back.rules.reveal_resources);
    }
    CHECK(!EndlessSetup::original().rules.reveal_resources);
    CHECK(!app::setup_item_offered(app::SetupScope::Scenario, SetupItem::RevealResources));
    CHECK(!s.rules.bugs.classic_ai);  // Open1602's computer players by default
    app::choose(s, SetupItem::ClassicAi, 1);
    CHECK(s.rules.bugs.classic_ai);
    CHECK(s.rules.pirates == sim::Pirates::Game);  // as the level by default: none on the two easy ones
    CHECK(value_of(s, SetupItem::Pirates) == ui::tr("setup.pirates.game"));
    app::choose(s, SetupItem::Pirates, 2);
    CHECK(s.rules.pirates == sim::Pirates::On);
    CHECK(app::write_setup(s).find("endless.pirates = on\n") != std::string::npos);
    EndlessSetup read;
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.pirates == sim::Pirates::On);
    CHECK(app::read_setup_line(read, "pirates", "off"));
    CHECK(read.rules.pirates == sim::Pirates::Off);
    CHECK(app::read_setup_line(read, "pirates", "many"));  // not a value: unchanged
    CHECK(read.rules.pirates == sim::Pirates::Off);
    // Pirate nests: one for every four players by default (up to four players the original's one).
    CHECK(s.rules.pirate_nests == sim::PirateNests::Scaled);
    CHECK(value_of(s, SetupItem::PirateNests) == ui::tr("setup.pirate_nests.per4"));
    app::choose(s, SetupItem::PirateNests, 0);
    CHECK(s.rules.pirate_nests == sim::PirateNests::One);
    CHECK(app::write_setup(s).find("endless.pirate_nests = one\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.pirate_nests == sim::PirateNests::One);
    CHECK(app::read_setup_line(read, "pirate_nests", "per4"));
    CHECK(read.rules.pirate_nests == sim::PirateNests::Scaled);
    // The towns known from the start: as the original by default, every island is discovered by
    // ship, but a player's town shows its island once it is founded.
    CHECK(s.rules.reveal == sim::Reveal::Original);
    CHECK(value_of(s, SetupItem::Reveal) == ui::tr("setup.reveal.original"));
    app::choose(s, SetupItem::Reveal, 0);
    CHECK(s.rules.reveal == sim::Reveal::None);
    CHECK(value_of(s, SetupItem::Reveal) == ui::tr("setup.reveal.none"));
    CHECK(app::write_setup(s).find("endless.reveal = none\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.reveal == sim::Reveal::None);
    app::choose(s, SetupItem::Reveal, 2);
    CHECK(s.rules.reveal == sim::Reveal::All);
    CHECK(app::write_setup(s).find("endless.reveal = all\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.reveal == sim::Reveal::All);
    CHECK(app::read_setup_line(read, "reveal", "original"));
    CHECK(read.rules.reveal == sim::Reveal::Original);
    CHECK(app::read_setup_line(read, "reveal", "all"));
    CHECK(app::read_setup_line(read, "reveal", "players"));  // the towns of players of version 0.2
    CHECK(read.rules.reveal == sim::Reveal::Original);
    CHECK(app::read_setup_line(read, "reveal", "some"));  // not a value: unchanged
    CHECK(read.rules.reveal == sim::Reveal::Original);
    CHECK(sim::Rules::original().reveal == sim::Reveal::Original);
    // The free trader's reach: as in a four-player game by default (up to four players the original).
    CHECK(s.rules.trader_reach == sim::TraderReach::Scaled);
    CHECK(value_of(s, SetupItem::TraderReach) == ui::tr("setup.trader_reach.per4"));
    app::choose(s, SetupItem::TraderReach, 0);
    CHECK(s.rules.trader_reach == sim::TraderReach::Original);
    CHECK(app::write_setup(s).find("endless.trader_reach = original\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.trader_reach == sim::TraderReach::Original);
    CHECK(app::read_setup_line(read, "trader_reach", "per4"));
    CHECK(read.rules.trader_reach == sim::TraderReach::Scaled);
    // Large ore veins: as the game file says by default, as in the original.
    CHECK(s.rules.ore_veins == sim::OreVeins::Game);
    CHECK(value_of(s, SetupItem::OreVeins) == ui::tr("setup.ore_veins.game"));
    app::choose(s, SetupItem::OreVeins, 2);
    CHECK(s.rules.ore_veins == sim::OreVeins::Endless);
    CHECK(app::write_setup(s).find("endless.ore_veins = endless\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.ore_veins == sim::OreVeins::Endless);
    CHECK(app::read_setup_line(read, "ore_veins", "finite"));
    CHECK(read.rules.ore_veins == sim::OreVeins::Finite);
    CHECK(app::read_setup_line(read, "ore_veins", "game"));
    CHECK(read.rules.ore_veins == sim::OreVeins::Game);
    CHECK(app::read_setup_line(read, "ore_veins", "original"));  // finite, as it was called before
    CHECK(read.rules.ore_veins == sim::OreVeins::Finite);
    // The computer opponents: as the game file says by default.
    CHECK(s.rules.computer_players == sim::Computers::Game);
    CHECK(value_of(s, SetupItem::ComputerPlayers) == ui::tr("setup.computers.game"));
    app::choose(s, SetupItem::ComputerPlayers, 1);
    CHECK(s.rules.computer_players == sim::Computers::Off);
    CHECK(app::write_setup(s).find("endless.computers = off\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.computer_players == sim::Computers::Off);
    CHECK(app::setup_item_offered(app::SetupScope::Scenario, SetupItem::ComputerPlayers));
    // Open1602's balance changes, each on its own: on by default, the original's settings off.
    CHECK(s.rules.revised_bread);
    CHECK(s.rules.wider_hunting);
    CHECK(s.rules.large_ship_holds);
    CHECK(app::balance_summary(s.rules) == ui::tr("setup.balance.all"));
    CHECK(value_of(s, SetupItem::BalanceBread) == ui::tr("setup.balance.bread.revised"));
    CHECK(value_of(s, SetupItem::BalanceHunting) == ui::tr("setup.balance.hunting.wider"));
    CHECK(value_of(s, SetupItem::BalanceShips) == ui::tr("setup.balance.ships.merchant"));
    app::choose(s, SetupItem::BalanceShips, 0);
    CHECK(!s.rules.large_ship_holds);
    CHECK(s.rules.revised_bread);
    CHECK(s.rules.wider_hunting);
    CHECK(app::balance_summary(s.rules) == ui::tr("setup.balance.some", {"2", "3"}));
    CHECK(app::write_setup(s).find("endless.large_ship_holds = original\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(!read.rules.large_ship_holds);
    CHECK(read.rules.revised_bread);
    CHECK(read.rules.wider_hunting);
    app::choose(s, SetupItem::BalanceBread, 0);
    CHECK(!s.rules.revised_bread);
    CHECK(s.rules.wider_hunting);
    CHECK(app::write_setup(s).find("endless.bread = original\n") != std::string::npos);
    CHECK(app::write_setup(s).find("endless.hunting = wider\n") != std::string::npos);
    app::choose(s, SetupItem::BalanceHunting, 0);
    CHECK(!s.rules.wider_hunting);
    CHECK(app::balance_summary(s.rules) == ui::tr("setup.balance.original"));
    s.rules = app::open1602_balance(s.rules);
    CHECK(s.rules.revised_bread);
    CHECK(s.rules.wider_hunting);
    CHECK(s.rules.large_ship_holds);
    // The one setting of before still reads, for both of its parts.
    CHECK(app::read_setup_line(read, "balance", "original"));
    CHECK(!read.rules.revised_bread);
    CHECK(!read.rules.wider_hunting);
    CHECK(app::read_setup_line(read, "balance", "alternative"));
    CHECK(read.rules.revised_bread);
    CHECK(read.rules.wider_hunting);
    CHECK(app::read_setup_line(read, "large_ship_holds", "merchant"));
    CHECK(read.rules.large_ship_holds);
    CHECK(app::setup_item_offered(app::SetupScope::Scenario, SetupItem::BalanceShips));
    CHECK(app::balance_item(SetupItem::BalanceHunting));
    CHECK(!app::balance_item(SetupItem::TradeCarts));
    // The original's settings switch them all off.
    EndlessSetup original;
    original.rules = sim::Rules::original();
    CHECK(app::setup_choice(original, SetupItem::BalanceBread) == 0);
    CHECK(app::setup_choice(original, SetupItem::BalanceHunting) == 0);
    CHECK(app::setup_choice(original, SetupItem::BalanceShips) == 0);
    CHECK(app::balance_summary(app::original_balance(s.rules)) == ui::tr("setup.balance.original"));
    CHECK(app::balance_summary(app::open1602_balance(original.rules)) == ui::tr("setup.balance.all"));
    // Trade carts per island: one per Kontor and market by default, one as in the original on choice.
    CHECK(s.rules.more_carts);
    CHECK(value_of(s, SetupItem::TradeCarts) == ui::tr("setup.trade_carts.more"));
    app::choose(s, SetupItem::TradeCarts, 0);
    CHECK(!s.rules.more_carts);
    CHECK(app::write_setup(s).find("endless.trade_carts = one\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(!read.rules.more_carts);
    CHECK(app::setup_item_offered(app::SetupScope::Scenario, SetupItem::TradeCarts));
    CHECK(!EndlessSetup::original().rules.more_carts);
    // Fertile fields: by the island by default and in the original, every field where the crop grows on choice.
    CHECK(!s.rules.fertile_fields);
    CHECK(value_of(s, SetupItem::FertileFields) == ui::tr("setup.fertile_fields.original"));
    app::choose(s, SetupItem::FertileFields, 1);
    CHECK(s.rules.fertile_fields);
    CHECK(app::write_setup(s).find("endless.fertile_fields = all\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.fertile_fields);
    CHECK(app::read_setup_line(read, "fertile_fields", "original"));
    CHECK(!read.rules.fertile_fields);
    CHECK(app::setup_item_offered(app::SetupScope::Scenario, SetupItem::FertileFields));
    CHECK(!EndlessSetup::original().rules.fertile_fields);
    // Moving buildings: free by default, like building anew or off on choice, off in the original.
    CHECK(s.rules.moving == sim::Moving::Free);
    CHECK(value_of(s, SetupItem::Moving) == ui::tr("setup.moving.free"));
    app::choose(s, SetupItem::Moving, 1);
    CHECK(s.rules.moving == sim::Moving::Rebuild);
    CHECK(value_of(s, SetupItem::Moving) == ui::tr("setup.moving.rebuild"));
    CHECK(app::write_setup(s).find("endless.moving = rebuild\n") != std::string::npos);
    app::read_setup(read, app::write_setup(s));
    CHECK(read.rules.moving == sim::Moving::Rebuild);
    CHECK(app::read_setup_line(read, "moving", "off"));
    CHECK(read.rules.moving == sim::Moving::Off);
    CHECK(app::setup_item_offered(app::SetupScope::Scenario, SetupItem::Moving));
    CHECK(EndlessSetup::original().rules.moving == sim::Moving::Off);

    // The world's items count only for a world of its own.
    CHECK(!app::setup_item_used(s, SetupItem::Players));
    CHECK(app::setup_item_used(s, SetupItem::Resources));
    app::choose(s, SetupItem::Map, 1);
    CHECK(s.own_world);
    CHECK(app::setup_item_used(s, SetupItem::Players));
    app::choose(s, SetupItem::Players, 4);
    CHECK(s.world.players == 6);
    // The size follows the players at first, as in a multiplayer game.
    CHECK(s.auto_size);
    CHECK(s.world_settings().width == sim::settings_for_players(6).width);
    CHECK(s.world_settings().height == sim::settings_for_players(6).height);
    app::choose(s, SetupItem::WorldSize, 4);
    CHECK(!s.auto_size);
    CHECK(s.world_settings().width == 1000);
    CHECK(s.world_settings().height == 700);
    app::choose(s, SetupItem::Villages, 0);
    CHECK(s.world.natives == sim::WorldSettings::Amount::None);
    app::choose(s, SetupItem::Starts, 1);
    CHECK(s.world.starts == sim::WorldSettings::Starts::Together);
    app::choose(s, SetupItem::Islands, 99);  // no such value: unchanged
    CHECK(s.world.islands == sim::WorldSettings::Amount::Normal);

    // A setting between the offered values shows the nearest.
    EndlessSetup odd;
    odd.rules.max_ships_per_player = 150;
    odd.rules.demolish_refund = sim::Refund::Partial;
    odd.rules.refund_percent = 60;
    CHECK(app::setup_choice(odd, SetupItem::Ships) == 1);   // 100
    CHECK(app::setup_choice(odd, SetupItem::Refund) == 2);  // 50 %
}

TEST_CASE("endless setup: the original's rules and Open1602's defaults") {
    const EndlessSetup original = EndlessSetup::original();
    CHECK(original.rules == sim::Rules::original());
    CHECK(!original.own_world);
    CHECK(EndlessSetup{}.rules == sim::Rules::open1602());
    CHECK(!(original == EndlessSetup{}));
}

TEST_CASE("endless setup: settings.cfg keeps the last choice") {
    app::AppSettings a;
    app::choose(a.endless, SetupItem::Refund, 1);
    app::choose(a.endless, SetupItem::Units, 3);
    app::choose(a.endless, SetupItem::GameSpeed, 0);
    app::choose(a.endless, SetupItem::Droughts, 2);
    app::choose(a.endless, SetupItem::Map, 1);
    app::choose(a.endless, SetupItem::WorldSize, 5);
    app::choose(a.endless, SetupItem::Players, 6);
    app::choose(a.endless, SetupItem::IslandSizes, 2);
    app::choose(a.endless, SetupItem::VolcanoIslands, 3);
    const std::string text = app::write_settings(a);
    CHECK(text.find("endless.world = 1400x980\n") != std::string::npos);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.endless == a.endless);
    CHECK(back.endless.rules.refund_percent == 25);

    // Wrong values keep the setting; other keys are no business of the setup.
    EndlessSetup s;
    CHECK(app::read_setup_line(s, "world", "100x100"));
    CHECK(s.world.width == 500);
    CHECK(app::read_setup_line(s, "islands", "none"));  // an endless game needs islands
    CHECK(s.world.islands == sim::WorldSettings::Amount::Normal);
    CHECK(app::read_setup_line(s, "droughts", "always"));
    CHECK(s.rules.droughts == sim::Droughts::Game);
    CHECK(app::read_setup_line(s, "players", "40"));
    CHECK(s.world.players == 4);
    CHECK(!app::read_setup_line(s, "colour", "red"));
    CHECK(s == EndlessSetup{});

    // The automatic size is kept as such.
    CHECK(app::write_setup(EndlessSetup{}).find("endless.world = auto\n") != std::string::npos);
    CHECK(app::read_setup_line(s, "world", "700x490"));
    CHECK(!s.auto_size);
    CHECK(app::read_setup_line(s, "world", "auto"));
    CHECK(s.auto_size);
    CHECK(s.world.width == 700);  // the last fixed size stays for the next time
}

TEST_CASE("endless setup: the automatic size grows with more than four players") {
    EndlessSetup s;
    s.own_world = true;
    for (std::uint8_t players = 2; players <= 4; ++players) {
        s.world.players = players;
        CHECK(s.world_settings().width == 500);
        CHECK(s.world_settings().height == 350);
    }
    s.world.players = 8;
    const sim::WorldSettings eight = s.world_settings();
    CHECK(eight.width == sim::settings_for_players(8).width);
    CHECK(eight.width > 500);
    CHECK(eight.players == 8);
    CHECK(app::setup_value(s, SetupItem::WorldSize).find(std::to_string(eight.width)) != std::string::npos);

    // Up to twelve players, as in multiplayer; a fixed size stays as chosen.
    app::choose(s, SetupItem::Players, 10);
    CHECK(s.world.players == 12);
    CHECK(s.world_settings().width == sim::settings_for_players(12).width);
    app::choose(s, SetupItem::WorldSize, 2);
    CHECK(s.world_settings().width == 500);
    CHECK(s.world_settings().players == 12);
}

TEST_CASE("endless setup: the dialog chooses, resets and starts") {
    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{});
    // A click on a value takes the next one.
    CHECK(click(menu, value_of(menu.setup(), SetupItem::Refund)) == app::GameSettingsMenu::Result::None);
    CHECK(menu.setup().rules.demolish_refund == sim::Refund::Partial);
    // The world's buttons do nothing on the original's maps.
    const std::string players = value_of(menu.setup(), SetupItem::Players);
    (void)click(menu, players);
    CHECK(menu.setup().world.players == 4);
    (void)click(menu, ui::tr("setup.map.original"));
    CHECK(menu.setup().own_world);
    (void)click(menu, players);
    CHECK(menu.setup().world.players == 5);
    // A right click takes the previous value; buttons that are no setting do not answer it.
    CHECK(click(menu, value_of(menu.setup(), SetupItem::Players), ui::MouseButton::Right) ==
          app::GameSettingsMenu::Result::None);
    CHECK(menu.setup().world.players == 4);
    (void)click(menu, value_of(menu.setup(), SetupItem::Refund), ui::MouseButton::Right);
    CHECK(menu.setup().rules.demolish_refund == EndlessSetup{}.rules.demolish_refund);
    CHECK(click(menu, ui::tr("setup.start"), ui::MouseButton::Right) == app::GameSettingsMenu::Result::None);
    CHECK(click(menu, ui::tr("setup.back"), ui::MouseButton::Right) == app::GameSettingsMenu::Result::None);

    CHECK(click(menu, ui::tr("setup.original")) == app::GameSettingsMenu::Result::None);
    CHECK(menu.setup() == EndlessSetup::original());
    (void)click(menu, ui::tr("setup.defaults"));
    CHECK(menu.setup() == EndlessSetup{});
    CHECK(click(menu, ui::tr("setup.start")) == app::GameSettingsMenu::Result::Start);
    CHECK(click(menu, ui::tr("setup.back")) == app::GameSettingsMenu::Result::Back);
}

TEST_CASE("endless setup: the dialog speaks English and German") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        CHECK(ui::tr("setup.title") != "setup.title");
        for (std::size_t i = 0; i < app::kSetupItems; ++i) {
            const auto item = static_cast<SetupItem>(i);
            CHECK(app::setup_label(item).find("setup.") == std::string::npos);
            for (std::size_t c = 0; c < app::setup_choices(item); ++c) {
                CHECK(app::setup_value(item, c).find("setup.") == std::string::npos);
            }
        }
    }
    CHECK(ui::tr("setup.start") == "Start game");
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("game setup: what a scenario, a multiplayer game and a savegame offer") {
    using app::SetupScope;
    // A scenario's world is given: only the rules.
    for (const SetupItem item : {SetupItem::Map, SetupItem::WorldSize, SetupItem::Players, SetupItem::Islands,
                                 SetupItem::Starts, SetupItem::Resources}) {
        CHECK(!app::setup_item_offered(SetupScope::Scenario, item));
    }
    for (const SetupItem item :
         {SetupItem::Refund, SetupItem::TraderPrices, SetupItem::TraderReach, SetupItem::OreVeins,
          SetupItem::BalanceBread, SetupItem::BalanceShips, SetupItem::Ships, SetupItem::Units, SetupItem::GameSpeed,
          SetupItem::Droughts, SetupItem::Volcanoes, SetupItem::ClassicAi, SetupItem::Pirates, SetupItem::PirateNests,
          SetupItem::Reveal}) {
        CHECK(app::setup_item_offered(SetupScope::Scenario, item));
        CHECK(app::setup_item_open(EndlessSetup{}, SetupScope::Scenario, item));
        CHECK(!app::setup_item_open(EndlessSetup{}, SetupScope::Savegame, item));  // to look at only
    }
    // A multiplayer endless game: everything but the players, which are the lobby's.
    EndlessSetup mp = app::multiplayer_setup(EndlessSetup::defaults(SetupScope::MultiplayerEndless), 4);
    CHECK(!mp.own_world);
    CHECK(mp.world.starts == sim::WorldSettings::Starts::Together);
    CHECK(app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::Map));
    CHECK(!app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::Players));
    CHECK(app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::ClassicAi));
    // More than four always get a world of their own.
    mp = app::multiplayer_setup(mp, 7);
    CHECK(mp.own_world);
    CHECK(mp.world.players == 7);
    CHECK(mp.world_settings().width == sim::settings_for_players(7).width);
    CHECK(!app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::Map));
    CHECK(app::setup_item_open(mp, SetupScope::MultiplayerEndless, SetupItem::Islands));

    // A scenario compares the rules only.
    EndlessSetup a;
    EndlessSetup b;
    b.own_world = true;
    CHECK(app::same_setup(a, b, SetupScope::Scenario));
    CHECK(!app::same_setup(a, b, SetupScope::Endless));
}

TEST_CASE("game setup: settings.cfg keeps the scenario's and the host's choice apart") {
    app::AppSettings a;
    app::choose(a.scenario, SetupItem::ClassicAi, 1);
    app::choose(a.scenario, SetupItem::TraderPrices, 1);
    app::choose(a.multiplayer, SetupItem::Map, 1);
    app::choose(a.multiplayer, SetupItem::Villages, 0);
    const std::string text = app::write_settings(a);
    CHECK(text.find("scenario.ai = classic\n") != std::string::npos);
    CHECK(text.find("multiplayer.own_world = on\n") != std::string::npos);
    const app::AppSettings back = app::parse_settings(text, app::AppSettings{});
    CHECK(back.scenario == a.scenario);
    CHECK(back.multiplayer == a.multiplayer);
    CHECK(back.endless == EndlessSetup{});
    // A host's world of its own starts together by default, as the original's multiplayer maps.
    CHECK(app::AppSettings{}.multiplayer.world.starts == sim::WorldSettings::Starts::Together);

    // The same lines, as text with any prefix.
    EndlessSetup read = EndlessSetup::defaults(app::SetupScope::MultiplayerEndless);
    app::read_setup(read, "# a comment\n" + app::write_setup(a.multiplayer, "x.") + "x.colour = red\nbroken\n", "x.");
    CHECK(read == a.multiplayer);
}

TEST_CASE("game setup: the dialog of a scenario shows the rules only") {
    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{}, app::SetupScope::Scenario);
    CHECK(find_text(menu, ui::tr("setup.title.scenario")).has_value());
    CHECK(find_text(menu, ui::tr("setup.group.opponents")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.group.world")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.map")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.resources")).has_value());
    // The computer players are with the opponents, not on the page of the original's bugs, whose
    // presets switch them all the same.
    CHECK(find_text(menu, app::setup_label(SetupItem::ClassicAi)).has_value());
    (void)click(menu, ui::tr("setup.bug.classic_ai.fixed"));
    CHECK(menu.setup().rules.bugs.classic_ai);
    (void)click(menu, ui::tr("setup.bugs.open", {ui::tr("setup.bugs.none")}));  // not counted there
    REQUIRE(menu.bugs_page());
    CHECK(!find_text(menu, app::setup_label(SetupItem::ClassicAi)).has_value());
    (void)click(menu, ui::tr("setup.bugs.all_fixed"));
    CHECK(!menu.setup().rules.bugs.classic_ai);
    (void)click(menu, ui::tr("setup.bugs.all_original"));
    CHECK(menu.setup().rules.bugs.classic_ai);
    (void)click(menu, ui::tr("setup.back"));
    CHECK(find_text(menu, ui::tr("setup.bug.classic_ai.original")).has_value());
    (void)click(menu, ui::tr("setup.defaults"));
    (void)click(menu, ui::tr("setup.original"));
    CHECK(menu.setup().rules == sim::Rules::original());
    CHECK(click(menu, ui::tr("setup.start")) == app::GameSettingsMenu::Result::Start);
}

TEST_CASE("game setup: the host's dialog keeps the lobby's players, the others only look") {
    app::GameSettingsMenu menu;
    const EndlessSetup six = app::multiplayer_setup(EndlessSetup::defaults(app::SetupScope::MultiplayerEndless), 6);
    menu.open(six, app::SetupScope::MultiplayerEndless, app::GameSettingsMenu::Mode::Apply);
    CHECK(find_text(menu, ui::tr("setup.title.multiplayer")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.start")).has_value());
    (void)click(menu, "6");  // the players: the lobby's
    CHECK(menu.setup().world.players == 6);
    (void)click(menu, ui::tr("setup.map.own"));  // more than four: always a world of their own
    CHECK(menu.setup().own_world);
    (void)click(menu, ui::tr("setup.original"));
    CHECK(menu.setup().rules == sim::Rules::original());
    CHECK(menu.setup().world.players == 6);
    CHECK(menu.setup().own_world);
    CHECK(click(menu, ui::tr("setup.apply")) == app::GameSettingsMenu::Result::Start);

    // Looking only: the values are text, no presets, no start.
    menu.open(six, app::SetupScope::MultiplayerEndless, app::GameSettingsMenu::Mode::View);
    const std::string refund = value_of(six, SetupItem::Refund);
    CHECK(click(menu, refund) == app::GameSettingsMenu::Result::None);
    CHECK(menu.setup() == six);
    CHECK(!find_text(menu, ui::tr("setup.original")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.apply")).has_value());
    CHECK(click(menu, ui::tr("setup.back")) == app::GameSettingsMenu::Result::Back);
    // A savegame's settings can only be looked at, whatever the caller asks.
    menu.open(EndlessSetup{}, app::SetupScope::Savegame, app::GameSettingsMenu::Mode::Apply);
    CHECK(menu.mode() == app::GameSettingsMenu::Mode::View);
}

TEST_CASE("game setup: the texts of every kind of game in English and German") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        for (const std::string_view key : {"setup.title.multiplayer", "setup.title.scenario", "setup.title.savegame",
                                           "setup.hint.multiplayer", "setup.hint.scenario", "setup.hint.savegame",
                                           "setup.apply", "setup.custom", "setup.change", "setup.view"}) {
            CHECK(ui::tr(key) != key);
        }
        CHECK(ui::tr("setup.summary", {"x"}).find('x') != std::string::npos);
    }
    CHECK(ui::tr("setup.apply") == "Apply");
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("endless setup: the gold islands, automatic or a fixed number") {
    EndlessSetup s;
    CHECK(s.world.gold_islands == 0);
    CHECK(value_of(s, SetupItem::GoldIslands) == ui::tr("setup.gold_islands.auto"));
    CHECK(!app::setup_item_used(s, SetupItem::GoldIslands));  // the original's maps keep their gold
    s.own_world = true;
    CHECK(app::setup_item_used(s, SetupItem::GoldIslands));
    app::choose(s, SetupItem::GoldIslands, 3);
    CHECK(s.world.gold_islands == 3);
    CHECK(value_of(s, SetupItem::GoldIslands) == "3");
    CHECK(s.world_settings().gold_islands == 3);
    CHECK(!(s == EndlessSetup{}));
    app::choose(s, SetupItem::GoldIslands, app::setup_choices(SetupItem::GoldIslands) - 1);
    CHECK(s.world.gold_islands == 40);  // the most, for the largest maps
    app::choose(s, SetupItem::GoldIslands, 3);

    // settings.cfg, and with it the lobby, keeps the choice.
    CHECK(app::write_setup(s).find("endless.gold_islands = 3\n") != std::string::npos);
    CHECK(app::write_setup(EndlessSetup{}).find("endless.gold_islands = auto\n") != std::string::npos);
    EndlessSetup back;
    app::read_setup(back, app::write_setup(s, "multiplayer."), "multiplayer.");
    CHECK(back == s);
    CHECK(app::read_setup_line(back, "gold_islands", "auto"));
    CHECK(back.world.gold_islands == 0);
    CHECK(app::read_setup_line(back, "gold_islands", "7"));  // between the dialog's values
    CHECK(back.world.gold_islands == 7);
    CHECK(app::setup_choice(back, SetupItem::GoldIslands) == app::setup_choice(s, SetupItem::GoldIslands) + 3);
    CHECK(app::read_setup_line(back, "gold_islands", "0"));  // not a number of islands: unchanged
    CHECK(app::read_setup_line(back, "gold_islands", "999"));
    CHECK(app::read_setup_line(back, "gold_islands", "many"));
    CHECK(back.world.gold_islands == 7);
}

TEST_CASE("endless setup: island shapes, the original's templates by default") {
    EndlessSetup s;
    CHECK(s.world.shapes == sim::WorldSettings::Shapes::Templates);
    CHECK(value_of(s, SetupItem::IslandShapes) == ui::tr("setup.island_shapes.templates"));
    CHECK(!app::setup_item_used(s, SetupItem::IslandShapes));  // the original's maps keep their templates
    s.own_world = true;
    CHECK(app::setup_item_used(s, SetupItem::IslandShapes));
    app::choose(s, SetupItem::IslandShapes, 2);
    CHECK(s.world.shapes == sim::WorldSettings::Shapes::Own);
    CHECK(s.world_settings().shapes == sim::WorldSettings::Shapes::Own);
    CHECK(!(s == EndlessSetup{}));
    CHECK(app::write_setup(s).find("endless.island_shapes = own\n") != std::string::npos);
    CHECK(app::write_setup(EndlessSetup{}).find("endless.island_shapes = templates\n") != std::string::npos);
    EndlessSetup back;
    app::read_setup(back, app::write_setup(s));
    CHECK(back == s);
    CHECK(app::read_setup_line(back, "island_shapes", "mixed"));
    CHECK(back.world.shapes == sim::WorldSettings::Shapes::Mixed);
    CHECK(app::read_setup_line(back, "island_shapes", "round"));  // unknown: unchanged
    CHECK(back.world.shapes == sim::WorldSettings::Shapes::Mixed);
}

TEST_CASE("game setup: every group of the dialog ends above its hint") {
    for (const auto scope :
         {app::SetupScope::Endless, app::SetupScope::MultiplayerEndless, app::SetupScope::Scenario}) {
        app::GameSettingsMenu menu;
        menu.open(EndlessSetup{}, scope);
        // The hint's first line lies below every item's row.
        const std::string hint = ui::tr(scope == app::SetupScope::Endless    ? "setup.hint"
                                        : scope == app::SetupScope::Scenario ? "setup.hint.scenario"
                                                                             : "setup.hint.multiplayer");
        std::int32_t hint_y = INT32_MAX;
        std::int32_t rows_y = 0;
        for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
            if (d.kind != ui::DrawItem::Kind::Text || d.text.empty()) {
                continue;
            }
            if (d.text.size() > 20 && hint.compare(0, 20, d.text, 0, 20) == 0) {  // fitted to its room
                hint_y = std::min(hint_y, d.y);
            } else if (d.text == app::setup_label(SetupItem::Reveal)) {  // the last row of the lower column
                rows_y = d.y;
            }
        }
        REQUIRE(hint_y != INT32_MAX);
        CHECK(rows_y > 0);
        CHECK(rows_y + 27 <= hint_y);  // a row is 28 high, the hint's line 22: texts sit in their middles
        if (scope != app::SetupScope::Scenario) {
            CHECK(find_text(menu, ui::tr("setup.gold_islands")).has_value());
        }
    }
}

TEST_CASE("endless setup: the victory conditions, all off by default") {
    EndlessSetup s;
    CHECK(!s.rules.victory.active());
    CHECK(app::victory_summary(s.rules.victory) == ui::tr("setup.victory.none"));
    CHECK(app::victory_text(s.rules.victory).empty());
    for (std::size_t i = 0; i < app::kSetupItems; ++i) {
        const auto item = static_cast<SetupItem>(i);
        if (app::victory_item(item) && item != SetupItem::WinClass && item != SetupItem::WinAll &&
            item != SetupItem::WinComputers) {
            CHECK(value_of(s, item) == ui::tr("setup.off"));
        }
    }
    // A few conditions: they reach the rules.
    app::choose(s, SetupItem::WinInhabitants, 3);
    app::choose(s, SetupItem::WinClass, 3);
    app::choose(s, SetupItem::WinClassInhabitants, 4);
    app::choose(s, SetupItem::WinTimeLimit, 2);
    app::choose(s, SetupItem::WinMonument, 2);
    const sim::VictoryConditions& v = s.rules.victory;
    CHECK(v.inhabitants == 5000);
    CHECK(v.resident_class == 4);  // aristocrats
    CHECK(v.class_inhabitants == 1000);
    CHECK(v.time_limit_minutes == 120);
    CHECK(v.monument == sim::Monument::Cathedral);
    CHECK(app::victory_count(v) == 4);
    CHECK(app::victory_summary(v) == ui::tr("setup.victory.many", {"4"}));
    CHECK(app::setup_item_used(s, SetupItem::WinClass));
    CHECK(app::setup_item_used(s, SetupItem::WinAll));
    CHECK(!(s == EndlessSetup{}));

    // settings.cfg, and with it the lobby, keeps them; any number, not only the dialog's.
    EndlessSetup back;
    app::read_setup(back, app::write_setup(s, "multiplayer."), "multiplayer.");
    CHECK(back == s);
    CHECK(app::read_setup_line(back, "victory.inhabitants", "7500"));
    CHECK(back.rules.victory.inhabitants == 7500);
    CHECK(app::setup_choice(back, SetupItem::WinInhabitants) != 0);  // shown as a value that is on
    CHECK(app::read_setup_line(back, "victory.islands", "150"));     // beyond 100 %: unchanged
    CHECK(back.rules.victory.islands_percent == 0);
    CHECK(app::read_setup_line(back, "victory.monument", "both"));
    CHECK(back.rules.victory.monument == sim::Monument::Both);
    CHECK(app::read_setup_line(back, "victory.combine", "any"));
    CHECK(!back.rules.victory.all);
    CHECK(!app::read_setup_line(back, "victory.unknown", "1"));

    // The mission window's sentence names them with their values.
    ui::set_language("de");
    const std::string text = app::victory_text(back.rules.victory);
    CHECK(text.find("Siegbedingungen") == 0);
    CHECK(text.find("7.500") != std::string::npos);
    CHECK(text.find("2\xA0Std.") != std::string::npos);  // a no-break space: never wrapped apart
    ui::set_language(ui::kFallbackLanguage);
    CHECK(app::victory_text(back.rules.victory).find("7,500") != std::string::npos);
}

TEST_CASE("game setup: victory conditions for endless games, not for scenarios") {
    using app::SetupScope;
    CHECK(app::victory_offered(SetupScope::Endless));
    CHECK(app::victory_offered(SetupScope::MultiplayerEndless));
    CHECK(app::victory_offered(SetupScope::Savegame));   // to look at
    CHECK(!app::victory_offered(SetupScope::Scenario));  // a scenario has its own missions
    CHECK(!app::setup_item_offered(SetupScope::Scenario, SetupItem::WinInhabitants));
    CHECK(app::setup_item_open(EndlessSetup{}, SetupScope::Endless, SetupItem::WinInhabitants));
    CHECK(!app::setup_item_open(EndlessSetup{}, SetupScope::Savegame, SetupItem::WinInhabitants));

    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{}, SetupScope::Scenario);
    CHECK(!find_text(menu, ui::tr("setup.victory")).has_value());
}

TEST_CASE("game setup: the page of the victory conditions") {
    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{});
    const std::string open = ui::tr("setup.victory.open", {ui::tr("setup.victory.none")});
    CHECK(!find_text(menu, ui::tr("setup.win.inhabitants")).has_value());
    CHECK(click(menu, open) == app::GameSettingsMenu::Result::None);
    CHECK(menu.victory_page());
    CHECK(find_text(menu, ui::tr("setup.title.victory")).has_value());
    CHECK(!find_text(menu, ui::tr("setup.original")).has_value());  // the presets are the rules'

    // A click on a condition turns it on.
    (void)click(menu, value_of(menu.setup(), SetupItem::WinInhabitants));
    CHECK(menu.setup().rules.victory.inhabitants == 1000);
    // Many conditions show "off": the one in the row of its label.
    const auto label = find_text(menu, app::setup_label(SetupItem::WinEliminate));
    REQUIRE(label.has_value());
    (void)menu.mouse(label->first + 260, label->second - 4, true);
    (void)menu.mouse(label->first + 260, label->second - 4, false);
    CHECK(menu.setup().rules.victory.eliminate);
    // "Back" leads to the settings, where the button counts them.
    CHECK(click(menu, ui::tr("setup.back")) == app::GameSettingsMenu::Result::None);
    CHECK(!menu.victory_page());
    CHECK(find_text(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.many", {"2"})})).has_value());
    // The presets change the rules, the conditions stay.
    (void)click(menu, ui::tr("setup.original"));
    CHECK(menu.setup().rules.bugs.classic_ai);
    CHECK(menu.setup().rules.victory.inhabitants == 1000);
    // Escape goes back to the settings first, then out.
    (void)click(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.many", {"2"})}));
    CHECK(menu.back());
    CHECK(!menu.back());
    // "All off" switches every condition off.
    (void)click(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.many", {"2"})}));
    (void)click(menu, ui::tr("setup.victory.off"));
    CHECK(!menu.setup().rules.victory.active());
    CHECK(click(menu, ui::tr("setup.start")) == app::GameSettingsMenu::Result::Start);

    // A savegame's conditions can be looked at, not changed.
    EndlessSetup saved;
    saved.rules.victory.score = 25'000;
    menu.open(saved, app::SetupScope::Savegame);
    (void)click(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.one")}));
    CHECK(menu.victory_page());
    CHECK(find_text(menu, value_of(saved, SetupItem::WinScore)).has_value());
    CHECK(!find_text(menu, ui::tr("setup.victory.off")).has_value());
    // Opened anew, the dialog starts on its first page.
    menu.open(saved, app::SetupScope::Endless);
    CHECK(!menu.victory_page());
}

TEST_CASE("game setup: the victory page's groups end above its hint, in English and German") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        app::GameSettingsMenu menu;
        menu.open(EndlessSetup{});
        (void)click(menu, ui::tr("setup.victory.open", {ui::tr("setup.victory.none")}));
        REQUIRE(menu.victory_page());
        std::int32_t lowest = 0;
        std::int32_t hint_y = INT32_MAX;
        const std::string hint = ui::tr("setup.hint.victory");
        for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
            if (d.kind != ui::DrawItem::Kind::Text || d.text.empty()) {
                continue;
            }
            if (d.text.size() > 20 && hint.compare(0, 20, d.text, 0, 20) == 0) {
                hint_y = std::min(hint_y, d.y);
            }
            for (std::size_t i = 0; i < app::kSetupItems; ++i) {
                const auto item = static_cast<SetupItem>(i);
                if (app::victory_item(item) && d.text == app::setup_label(item)) {
                    lowest = std::max(lowest, d.y);
                }
            }
        }
        REQUIRE(hint_y != INT32_MAX);
        CHECK(lowest > 0);
        CHECK(lowest + 27 <= hint_y);
        CHECK(ui::tr("setup.hint.victory") != "setup.hint.victory");
    }
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("game setup: the page of the original's bugs, fixed by default") {
    EndlessSetup s;
    CHECK(s.rules.bugs == sim::OriginalBugs{});
    CHECK(app::bugs_summary(s.rules.bugs) == ui::tr("setup.bugs.none"));
    CHECK(EndlessSetup::original().rules.bugs == sim::OriginalBugs::all());
    // Every kind of game offers them, a scenario too; a savegame shows them.
    for (const auto scope : {app::SetupScope::Endless, app::SetupScope::MultiplayerEndless, app::SetupScope::Scenario,
                             app::SetupScope::Savegame}) {
        CHECK(app::setup_item_offered(scope, SetupItem::BugGallows));
        CHECK(app::setup_item_offered(scope, SetupItem::BugFireRisk));
        CHECK(app::setup_item_open(s, scope, SetupItem::BugGallows) == (scope != app::SetupScope::Savegame));
    }

    // The values reach the rules, and settings.cfg (and with it the lobby) keeps them.
    app::choose(s, SetupItem::BugGallows, 1);
    CHECK(s.rules.bugs.gallows);
    CHECK(!s.rules.bugs.fire_risk);
    CHECK(app::bugs_summary(s.rules.bugs) == ui::tr("setup.bugs.one"));
    const std::string text = app::write_setup(s, "multiplayer.");
    CHECK(text.find("multiplayer.bug.gallows = original\n") != std::string::npos);
    CHECK(text.find("multiplayer.bug.fire_risk = fixed\n") != std::string::npos);
    EndlessSetup back;
    app::read_setup(back, text, "multiplayer.");
    CHECK(back == s);
    CHECK(app::read_setup_line(back, "bug.fire_risk", "original"));
    CHECK(back.rules.bugs.fire_risk);
    CHECK(back.rules.bugs.count() == 2);
    // Every bug has its line, the computer players their old one too.
    const std::string all = app::write_setup(EndlessSetup::original());
    CHECK(all.find("endless.ai = classic\n") != std::string::npos);
    CHECK(all.find("endless.bug.goldsheep = original\n") != std::string::npos);
    EndlessSetup all_back;
    app::read_setup(all_back, all);
    CHECK(all_back.rules.bugs == sim::OriginalBugs::all());
    CHECK(app::read_setup_line(back, "bug.gallows", "sometimes"));  // unknown: kept
    CHECK(back.rules.bugs.gallows);

    // The dialog: a button below the disasters opens the page, which has its own presets.
    for (const auto scope : {app::SetupScope::Endless, app::SetupScope::Scenario}) {
        app::GameSettingsMenu menu;
        menu.open(EndlessSetup{}, scope);
        CHECK(!find_text(menu, app::setup_label(SetupItem::BugGallows)).has_value());
        CHECK(find_text(menu, ui::tr("setup.bugs")).has_value());
        CHECK(click(menu, ui::tr("setup.bugs.open", {ui::tr("setup.bugs.none")})) ==
              app::GameSettingsMenu::Result::None);
        CHECK(menu.bugs_page());
        CHECK(!menu.victory_page());
        CHECK(find_text(menu, ui::tr("setup.title.bugs")).has_value());
        CHECK(!find_text(menu, ui::tr("setup.original")).has_value());  // the presets are the rules'
        (void)click(menu, value_of(menu.setup(), SetupItem::BugFireRisk));
        CHECK(menu.setup().rules.bugs.fire_risk);
        (void)click(menu, ui::tr("setup.bugs.all_original"));
        CHECK(menu.setup().rules.bugs == sim::OriginalBugs::all());
        CHECK(menu.setup().rules.victory == sim::VictoryConditions{});  // nothing else changes
        CHECK(menu.setup().rules.max_army == sim::Rules{}.max_army);
        (void)click(menu, ui::tr("setup.bugs.all_fixed"));
        CHECK(menu.setup().rules.bugs == sim::OriginalBugs{});
        (void)click(menu, value_of(menu.setup(), SetupItem::BugGallows));
        // Escape goes back to the settings first, where the button counts them.
        CHECK(menu.back());
        CHECK(!menu.bugs_page());
        CHECK(find_text(menu, ui::tr("setup.bugs.open", {ui::tr("setup.bugs.one")})).has_value());
        // The presets of the rules take the bugs with them.
        (void)click(menu, ui::tr("setup.defaults"));
        CHECK(menu.setup().rules.bugs == sim::OriginalBugs{});
        (void)click(menu, ui::tr("setup.original"));
        CHECK(menu.setup().rules.bugs == sim::OriginalBugs::all());
        CHECK(!menu.back());
    }

    // A savegame's can be looked at, not changed.
    EndlessSetup saved;
    saved.rules.bugs.gallows = true;
    app::GameSettingsMenu menu;
    menu.open(saved, app::SetupScope::Savegame);
    (void)click(menu, ui::tr("setup.bugs.open", {ui::tr("setup.bugs.one")}));
    CHECK(menu.bugs_page());
    CHECK(find_text(menu, value_of(saved, SetupItem::BugGallows)).has_value());
    CHECK(!find_text(menu, ui::tr("setup.bugs.all_fixed")).has_value());
}

TEST_CASE("game setup: the original's bugs' page ends above its hint, in English and German") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        app::GameSettingsMenu menu;
        menu.open(EndlessSetup{});
        (void)click(menu, ui::tr("setup.bugs.open", {ui::tr("setup.bugs.none")}));
        REQUIRE(menu.bugs_page());
        std::int32_t lowest = 0;
        std::int32_t hint_y = INT32_MAX;
        const std::string hint = ui::tr("setup.hint.bugs");
        for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
            if (d.kind != ui::DrawItem::Kind::Text || d.text.empty()) {
                continue;
            }
            if (d.text.size() > 20 && hint.compare(0, 20, d.text, 0, 20) == 0) {
                hint_y = std::min(hint_y, d.y);
            }
            for (std::size_t i = 0; i < app::kSetupItems; ++i) {
                const auto item = static_cast<SetupItem>(i);
                if (app::bug_item(item) && d.text == app::setup_label(item)) {
                    lowest = std::max(lowest, d.y);
                }
            }
        }
        REQUIRE(hint_y != INT32_MAX);
        CHECK(lowest > 0);
        CHECK(lowest + 27 <= hint_y);
        CHECK(ui::tr("setup.hint.bugs") != "setup.hint.bugs");
        CHECK(ui::tr("setup.title.bugs") != "setup.title.bugs");
    }
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("endless setup: a value none of the choices is shows as it is and steps to the nearest (LC-7)") {
    ui::set_language("en");
    EndlessSetup s;
    app::read_setup(s,
                    "endless.world = 2000x1800\nendless.players = 1\nendless.victory.minutes = 90\n"
                    "endless.game_speed = 64\nendless.ships = 150\n");
    CHECK(app::setup_value(s, SetupItem::WorldSize).find("2000") != std::string::npos);
    CHECK(app::setup_value(s, SetupItem::WorldSize).find("1800") != std::string::npos);
    CHECK(app::setup_value_custom(s, SetupItem::WorldSize));
    CHECK(app::setup_value(s, SetupItem::Players).find('1') != std::string::npos);
    CHECK(app::setup_value(s, SetupItem::GameSpeed).find("64") != std::string::npos);
    CHECK(app::setup_value(s, SetupItem::Ships).find("150") != std::string::npos);
    CHECK(app::setup_value(s, SetupItem::WinTimeLimit).find("90") != std::string::npos);
    // The text written and read again keeps the values.
    EndlessSetup again;
    app::read_setup(again, app::write_setup(s));
    CHECK(app::setup_value(again, SetupItem::WorldSize) == app::setup_value(s, SetupItem::WorldSize));
    CHECK(again.rules.max_game_speed == 64);
    // One step from a value of its own goes to the nearest offered one: one player becomes two.
    app::choose_next(s, SetupItem::Players);
    CHECK(s.world.players == 2);
    CHECK(!app::setup_value_custom(s, SetupItem::Players));
    app::choose_next(s, SetupItem::Players);
    CHECK(s.world.players == 3);
    // The nearest by area, then larger ones up to the generator's largest, 4000 x 4000.
    app::choose_next(s, SetupItem::WorldSize);
    CHECK(!app::setup_value_custom(s, SetupItem::WorldSize));
    CHECK(s.world.width == 2000);
    CHECK(s.world.height == 1400);
    app::choose_next(s, SetupItem::WorldSize);
    CHECK(s.world.width == 2800);
    app::choose_next(s, SetupItem::WorldSize);
    app::choose_next(s, SetupItem::WorldSize);
    CHECK(s.world.width == sim::WorldSettings::kMaxSide);
    CHECK(s.world.height == sim::WorldSettings::kMaxSide);
    EndlessSetup wide;
    app::read_setup(wide, "endless.world = 2000x1400\n");
    CHECK(!app::setup_value_custom(wide, SetupItem::WorldSize));
    // An offered value is shown without the mark.
    EndlessSetup plain;
    CHECK(!app::setup_value_custom(plain, SetupItem::Ships));
    CHECK(app::setup_value(plain, SetupItem::Ships) == value_of(plain, SetupItem::Ships));
}

TEST_CASE("endless setup: a multiplayer game runs at most the dialog's speed, pioneers do not win (LC-8, GB-6)") {
    EndlessSetup s;
    app::read_setup(s, "endless.game_speed = 64\nendless.victory.class = 0\n");
    CHECK(s.rules.max_game_speed == 64);         // alone it may
    CHECK(s.rules.victory.resident_class == 4);  // pioneers are refused, the default stays
    CHECK(app::multiplayer_setup(s, 6).rules.max_game_speed == 8);
    app::read_setup(s, "endless.victory.class = 1\n");
    CHECK(s.rules.victory.resident_class == 1);
}

TEST_CASE("game setup: a savegame's values take the room their labels leave, in English and German") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        app::GameSettingsMenu menu;
        menu.open(EndlessSetup{}, app::SetupScope::Savegame);
        for (auto i = 0; i < static_cast<int>(SetupItem::Count); ++i) {
            const auto item = static_cast<SetupItem>(i);
            if (!app::setup_item_offered(app::SetupScope::Savegame, item) || app::victory_item(item) ||
                app::bug_item(item) || app::disaster_item(item) || app::balance_item(item) ||
                item == SetupItem::Cheats) {  // the cheats: on the page of the bugs
                continue;
            }
            // Without fonts every byte is 9 units: wider than a button's room for the longest values.
            CHECK(find_text(menu, value_of(menu.setup(), item)).has_value());
        }
        (void)click(menu, ui::tr("setup.disasters.open", {ui::tr("setup.disasters.original")}));
        REQUIRE(menu.disasters_page());
        for (auto i = 0; i < static_cast<int>(SetupItem::Count); ++i) {
            const auto item = static_cast<SetupItem>(i);
            if (app::disaster_item(item)) {
                CHECK(find_text(menu, value_of(menu.setup(), item)).has_value());
            }
        }
        menu.back();
        (void)click(menu, ui::tr("setup.balance.open", {ui::tr("setup.balance.all")}));
        REQUIRE(menu.balance_page());
        for (auto i = 0; i < static_cast<int>(SetupItem::Count); ++i) {
            const auto item = static_cast<SetupItem>(i);
            if (app::balance_item(item)) {
                CHECK(find_text(menu, value_of(menu.setup(), item)).has_value());
            }
        }
    }
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("endless setup: the islands' presets and settings, and their lines in settings.cfg") {
    using Landscape = sim::WorldSettings::Landscape;
    EndlessSetup s;
    CHECK(app::islands_summary(s) == ui::tr("setup.island_preset.original"));
    // Only a world of its own has islands to shape; the shapes' items only for islands of their own.
    CHECK(!app::setup_item_used(s, SetupItem::IslandPreset));
    s.own_world = true;
    CHECK(app::setup_item_used(s, SetupItem::IslandPreset));
    CHECK(app::setup_item_used(s, SetupItem::Islands));
    CHECK(!app::setup_item_used(s, SetupItem::LandShare));
    CHECK(!app::setup_item_used(s, SetupItem::OreMountains));

    // Every preset after the original's is a world of islands of their own, matched again.
    for (std::size_t i = 1; i < app::setup_choices(SetupItem::IslandPreset); ++i) {
        app::choose(s, SetupItem::IslandPreset, i);
        CHECK(s.world.shapes == sim::WorldSettings::Shapes::Own);
        CHECK(app::setup_choice(s, SetupItem::IslandPreset) == i);
        CHECK(s.world.landscape.min_side <= s.world.landscape.max_side);
        CHECK(app::setup_item_used(s, SetupItem::LandShare));
        CHECK(!app::setup_item_used(s, SetupItem::Islands));  // the land decides how many
        CHECK(!app::setup_item_used(s, SetupItem::IslandSizes));
    }
    app::choose(s, SetupItem::IslandPreset, 1);
    CHECK(sim::same_islands(s.world.landscape, Landscape{}));
    CHECK(s.world.landscape.name == "varied");
    CHECK(app::islands_summary(s) == ui::tr("setup.island_preset.varied"));
    app::choose(s, SetupItem::IslandPreset, 0);
    CHECK(s.world.shapes == sim::WorldSettings::Shapes::Templates);

    // A setting of its own: "own settings".
    app::choose(s, SetupItem::IslandPreset, 3);  // continents
    app::choose(s, SetupItem::Coast, 2);
    CHECK(s.world.landscape.coast == sim::shapes::Roughness::Rugged);
    CHECK(app::setup_value(s, SetupItem::IslandPreset) == ui::tr("setup.island_preset.custom"));
    CHECK(app::islands_summary(s) == ui::tr("setup.island_preset.custom"));
    // The smallest island never larger than the largest: the other one follows.
    app::choose(s, SetupItem::LargestIsland, 0);  // 40
    CHECK(s.world.landscape.max_side == 40);
    CHECK(s.world.landscape.min_side <= 40);
    app::choose(s, SetupItem::SmallestIsland, app::setup_choices(SetupItem::SmallestIsland) - 1);  // 200
    CHECK(s.world.landscape.min_side == 200);
    CHECK(s.world.landscape.max_side == 200);
    app::choose(s, SetupItem::LandShare, 7);
    CHECK(s.world.landscape.land == 45);
    app::choose(s, SetupItem::Forest, 2);
    app::choose(s, SetupItem::OreMountains, 0);
    app::choose(s, SetupItem::SizeSpread, 2);
    app::choose(s, SetupItem::IslandGap, 0);
    app::choose(s, SetupItem::Outline, 1);
    CHECK(s.world.landscape.forest == sim::shapes::Share::Much);
    CHECK(s.world.landscape.mountains == sim::shapes::Share::Little);
    CHECK(s.world.landscape.spread == Landscape::Spread::FewGiants);
    CHECK(s.world.landscape.gap == Landscape::Gap::Narrow);
    CHECK(s.world.landscape.outline == Landscape::Outline::Long);
    CHECK(s.world_settings().landscape == s.world.landscape);
    CHECK(!(s == EndlessSetup{}));

    // settings.cfg keeps all of it; the world's number is not kept.
    s.world_seed = 1234;
    const std::string text = app::write_setup(s);
    CHECK(text.find("endless.island_land = 45\n") != std::string::npos);
    CHECK(text.find("endless.island_spread = giants\n") != std::string::npos);
    CHECK(text.find("endless.island_outline = long\n") != std::string::npos);
    // A changed preset keeps its name, the kind of island world (a climate's share there).
    CHECK(text.find("endless.island_preset = continents\n") != std::string::npos);
    CHECK(text.find("seed") == std::string::npos);
    EndlessSetup back;
    app::read_setup(back, text);
    CHECK(back == s);
    CHECK(back.world_seed == 0);
    CHECK(back.world.landscape == s.world.landscape);
    CHECK(back.world.landscape.name == "continents");
    // A setup of before the names: the settings of a preset are that preset.
    EndlessSetup old;
    old.own_world = true;
    app::choose(old, SetupItem::IslandPreset, 2);  // archipelago
    std::string unnamed = app::write_setup(old);
    unnamed.erase(unnamed.find("endless.island_preset = archipelago\n"), 36);
    EndlessSetup old_back;
    app::read_setup(old_back, unnamed);
    CHECK(old_back.world.landscape.name.empty());
    CHECK(app::setup_choice(old_back, SetupItem::IslandPreset) == 2);
    CHECK(sim::landscape_name(old_back.world.landscape) == "archipelago");
    // Values beyond the generator's are left out; any side within it is taken.
    CHECK(app::read_setup_line(back, "island_land", "90"));
    CHECK(back.world.landscape.land == 45);
    CHECK(app::read_setup_line(back, "island_max", "1200"));
    CHECK(back.world.landscape.max_side == 200);
    CHECK(app::read_setup_line(back, "island_max", "900"));  // far beyond the original's
    CHECK(back.world.landscape.max_side == 900);
    CHECK(app::read_setup_line(back, "island_max", "70"));
    CHECK(back.world.landscape.max_side == 70);
    CHECK(back.world.landscape.min_side == 70);  // follows
    CHECK(app::setup_value_custom(back, SetupItem::LargestIsland));
    CHECK(app::read_setup_line(back, "island_coast", "jagged"));  // unknown: kept
    CHECK(back.world.landscape.coast == sim::shapes::Roughness::Rugged);
}

namespace {

/// A world for the preview without the game's files: one island, two starts.
core::Result<sim::WorldPreview> fake_world(const EndlessSetup& setup, std::uint8_t /*level*/,
                                           const app::GameSettingsMenu::PreviewStop& /*stop*/) {
    sim::WorldPreview p;
    p.width = 100;
    p.height = 70;
    p.tiles.assign(100 * 70, sim::WorldPreview::Tile::Sea);
    for (std::uint32_t y = 20; y < 40; ++y) {
        for (std::uint32_t x = 30; x < 60; ++x) {
            p.tiles[y * 100 + x] = x == 30 ? sim::WorldPreview::Tile::Beach : sim::WorldPreview::Tile::Forest;
        }
    }
    p.islands = 1;
    p.land = 20 * 30;
    p.starts = {{10, 10}, {90, 60}};
    if (setup.world.landscape.land == 45) {
        return core::make_error(core::ErrorCode::LimitExceeded, "too much land");
    }
    return p;
}

}  // namespace

TEST_CASE("game setup: the page of the islands, with a preview of the world the game gets") {
    EndlessSetup s;
    s.own_world = true;
    app::GameSettingsMenu menu;
    menu.set_preview_maker(fake_world);
    app::GameSettingsMenu::PreviewColours colours{};
    for (std::size_t i = 0; i < colours.size(); ++i) {
        colours[i] = static_cast<std::uint8_t>(100 + i);
    }
    menu.set_preview_colours(colours);
    menu.open(s, app::SetupScope::Endless);
    // A row of the world's group opens the page; the page's items are not on the first one.
    CHECK(!find_text(menu, app::setup_label(SetupItem::Coast)).has_value());
    CHECK(click(menu, ui::tr("setup.island_page.open", {ui::tr("setup.island_preset.original")})) ==
          app::GameSettingsMenu::Result::None);
    CHECK(menu.islands_page());
    CHECK(find_text(menu, ui::tr("setup.title.islands")).has_value());
    CHECK(menu.setup().world_seed != 0);  // the preview's world is the game's
    menu.wait_preview();
    REQUIRE(menu.preview().has_value());
    CHECK(find_text(menu, ui::tr("setup.preview.stats", {"1", "9"})).has_value());  // 600 of 7000 tiles
    // The picture: the forest, the beach and the starts in their colours.
    std::array<bool, sim::WorldPreview::kTiles + 1> seen{};
    for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Fill && d.colour >= 100 && d.colour < 100 + static_cast<int>(seen.size())) {
            seen[static_cast<std::size_t>(d.colour - 100)] = true;
        }
    }
    CHECK(seen[static_cast<std::size_t>(sim::WorldPreview::Tile::Sea)]);
    CHECK(seen[static_cast<std::size_t>(sim::WorldPreview::Tile::Forest)]);
    CHECK(seen[static_cast<std::size_t>(sim::WorldPreview::Tile::Beach)]);
    CHECK(seen[sim::WorldPreview::kTiles]);
    CHECK(!seen[static_cast<std::size_t>(sim::WorldPreview::Tile::Mountain)]);

    // Another world: a new number, shown.
    const std::uint64_t first = menu.setup().world_seed;
    char text[24];
    std::snprintf(text, sizeof(text), "%llX", static_cast<unsigned long long>(first));
    CHECK(find_text(menu, ui::tr("setup.preview.seed", {text})).has_value());
    CHECK(click(menu, ui::tr("setup.preview.reroll")) == app::GameSettingsMenu::Result::None);
    CHECK(menu.setup().world_seed != first);

    // The presets and the items change the world; a world that cannot be made says why.
    (void)click(menu, value_of(menu.setup(), SetupItem::IslandPreset));
    CHECK(menu.setup().world.shapes == sim::WorldSettings::Shapes::Own);
    while (menu.setup().world.landscape.land != 45) {
        (void)click(menu, app::setup_value(menu.setup(), SetupItem::LandShare));
    }
    menu.wait_preview();
    CHECK(!menu.preview().has_value());
    CHECK(find_text(menu, ui::tr("setup.preview.failed", {"too much land"})).has_value());

    // Back to the settings, where the row names the page's choice; the game gets the world shown.
    CHECK(menu.back());
    CHECK(!menu.islands_page());
    CHECK(find_text(menu, ui::tr("setup.island_page.open", {ui::tr("setup.island_preset.custom")})).has_value());
    const std::uint64_t shown = menu.setup().world_seed;
    CHECK(click(menu, ui::tr("setup.start")) == app::GameSettingsMenu::Result::Start);
    CHECK(menu.setup().world_seed == shown);
}

TEST_CASE("game setup: the islands' page ends above its hint, in English and German, for the original's maps too") {
    for (const char* language : {"en", "de"}) {
        ui::set_language(language);
        for (const bool own : {true, false}) {
            EndlessSetup s;
            s.own_world = own;
            app::GameSettingsMenu menu;
            menu.open(s, app::SetupScope::Endless);
            (void)click(menu, ui::tr("setup.island_page.open", {ui::tr("setup.island_preset.original")}));
            REQUIRE(menu.islands_page());
            const std::string hint = ui::tr(own ? "setup.hint.islands" : "setup.hint.islands.original");
            std::int32_t hint_y = INT32_MAX;
            std::int32_t rows_y = 0;
            for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
                if (d.kind != ui::DrawItem::Kind::Text || d.text.empty()) {
                    continue;
                }
                if (d.text.size() > 20 && hint.compare(0, 20, d.text, 0, 20) == 0) {
                    hint_y = std::min(hint_y, d.y);
                } else if (d.text == app::setup_label(SetupItem::RevealResources)) {  // the last row
                    rows_y = d.y;
                }
            }
            REQUIRE(hint_y != INT32_MAX);
            CHECK(rows_y > 0);
            CHECK(rows_y + 27 <= hint_y);
            // Without a maker there is no preview, and no button to reroll is needed for it to work.
            CHECK(!menu.preview().has_value());
        }
    }
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("endless setup: kinds of island world of a package (landscapes.toml) come after Open1602's") {
    std::vector<sim::PackageLandscape> presets;
    std::vector<std::string> warnings;
    sim::apply_landscapes_file(presets, "seas",
                               "[atolls]\n"
                               "like = \"archipelago\"\n"
                               "title = \"Atolls\"\n"
                               "title.de = \"Atolle\"\n"
                               "land = 10\n"
                               "max = 40\n"
                               "[pangaea]\n"
                               "like = \"seas:atolls\"\n"
                               "land = 45\n"
                               "min = 400\n"
                               "spread = \"even\"\n"
                               "[bad]\n"
                               "like = \"nothing\"\n"  // line 13
                               "land = 90\n"           // line 14: out of range
                               "coast = \"wavy\"\n"    // line 15
                               "colour = 3\n"          // line 16
                               "[Bad]\n",              // line 17
                               warnings);
    REQUIRE(warnings.size() == 5);
    CHECK(warnings[0].starts_with("seas/landscapes.toml line 13: 'like' names no preset"));
    CHECK(warnings[1].starts_with("seas/landscapes.toml line 14: 'land' has a value out of its range"));
    CHECK(warnings[2].starts_with("seas/landscapes.toml line 15: 'coast'"));
    CHECK(warnings[3].starts_with("seas/landscapes.toml line 16: 'colour' is no field"));
    CHECK(warnings[4].starts_with("seas/landscapes.toml line 17: [Bad]"));
    REQUIRE(presets.size() == 3);
    const sim::PackageLandscape& atolls = presets[0];
    CHECK(atolls.id == "seas:atolls");
    CHECK(atolls.title("de") == "Atolle");
    CHECK(atolls.title("fr") == "Atolls");
    CHECK(atolls.landscape.land == 10);
    CHECK(atolls.landscape.max_side == 40);
    CHECK(atolls.landscape.min_side == sim::landscape_presets()[1].landscape.min_side);  // as the archipelago
    CHECK(atolls.landscape.gap == sim::landscape_presets()[1].landscape.gap);
    const sim::PackageLandscape& pangaea = presets[1];
    CHECK(pangaea.landscape.land == 45);
    CHECK(pangaea.landscape.min_side == 400);
    CHECK(pangaea.landscape.max_side == 400);  // the largest follows the smallest
    CHECK(pangaea.landscape.coast == atolls.landscape.coast);
    CHECK(pangaea.title("en") == "seas:pangaea");  // without a title: its id
    CHECK(sim::find_package_landscape(presets, "seas:bad") == &presets[2]);
    CHECK(sim::same_islands(presets[2].landscape, sim::WorldSettings::Landscape{}));
    CHECK(atolls.landscape.name == "seas:atolls");  // its own name, not the one of `like`
    CHECK(presets[2].landscape.name == "seas:bad");

    // In the start dialog after Open1602's presets, with their titles.
    const std::size_t before = app::setup_choices(SetupItem::IslandPreset);
    app::set_package_island_presets({atolls, pangaea});
    REQUIRE(app::setup_choices(SetupItem::IslandPreset) == before + 2);
    EndlessSetup s;
    s.own_world = true;
    app::choose(s, SetupItem::IslandPreset, before);
    CHECK(s.world.shapes == sim::WorldSettings::Shapes::Own);
    CHECK(s.world.landscape == atolls.landscape);
    CHECK(app::setup_choice(s, SetupItem::IslandPreset) == before);
    CHECK(app::setup_value(SetupItem::IslandPreset, before + 1) == "seas:pangaea");
    CHECK(app::islands_summary(s) == atolls.title(ui::current_language()));
    app::set_package_island_presets({});
    CHECK(app::setup_choices(SetupItem::IslandPreset) == before);
}

TEST_CASE("game setup: the page of the disasters and how often they come") {
    // Their lines in settings.cfg, and back.
    EndlessSetup s;
    s.rules.volcanoes = sim::Volcanoes::Rare;
    s.rules.plagues = sim::Outbreaks::Off;
    s.rules.fires = sim::Outbreaks::Rare;
    const std::string lines = app::write_setup(s);
    CHECK(lines.find("endless.volcanoes = rare\n") != std::string::npos);
    CHECK(lines.find("endless.plagues = off\n") != std::string::npos);
    CHECK(lines.find("endless.fires = rare\n") != std::string::npos);
    CHECK(lines.find("event_speed") == std::string::npos);  // an option of each computer now
    EndlessSetup back;
    app::read_setup(back, lines);
    CHECK(back == s);
    CHECK(app::read_setup_line(back, "plagues", "often"));  // unknown: kept
    CHECK(back.rules.plagues == sim::Outbreaks::Off);
    CHECK(app::read_setup_line(back, "event_speed", "keep"));  // a line of before: dropped quietly
    CHECK(back == s);
    CHECK(app::original_disasters(s.rules) == sim::Rules{});
    CHECK(EndlessSetup::original().rules == app::original_disasters(EndlessSetup::original().rules));

    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        for (const auto scope : {app::SetupScope::Endless, app::SetupScope::Scenario}) {
            app::GameSettingsMenu menu;
            menu.open(EndlessSetup{}, scope);
            // On the settings' page only the way there.
            CHECK(!find_text(menu, app::setup_label(SetupItem::Plagues)).has_value());
            CHECK(!find_text(menu, app::setup_label(SetupItem::Droughts)).has_value());
            CHECK(click(menu, ui::tr("setup.disasters.open", {ui::tr("setup.disasters.original")})) ==
                  app::GameSettingsMenu::Result::None);
            REQUIRE(menu.disasters_page());
            CHECK(find_text(menu, ui::tr("setup.title.disasters")).has_value());
            CHECK(!find_text(menu, ui::tr("setup.original")).has_value());
            (void)click(menu, ui::tr("setup.outbreaks.original"));  // the first: the plague
            CHECK(menu.setup().rules.plagues == sim::Outbreaks::Rare);
            (void)click(menu, ui::tr("setup.outbreaks.rare"));
            CHECK(menu.setup().rules.plagues == sim::Outbreaks::Off);
            (void)click(menu, ui::tr("setup.outbreaks.original"));  // now the fires
            CHECK(menu.setup().rules.fires == sim::Outbreaks::Rare);
            (void)click(menu, ui::tr("setup.outbreaks.rare"), ui::MouseButton::Right);
            CHECK(menu.setup().rules.fires == sim::Outbreaks::Original);
            (void)click(menu, ui::tr("setup.outbreaks.original"));

            // Every row ends above the hint.
            std::int32_t lowest = 0;
            std::int32_t hint_y = INT32_MAX;
            const std::string hint = ui::tr("setup.hint.disasters");
            for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
                if (d.kind != ui::DrawItem::Kind::Text || d.text.empty()) {
                    continue;
                }
                if (d.text.size() > 20 && hint.compare(0, 20, d.text, 0, 20) == 0) {
                    hint_y = std::min(hint_y, d.y);
                } else if (d.text == app::setup_label(SetupItem::Fires)) {
                    lowest = std::max(lowest, d.y);
                }
            }
            CHECK(lowest > 0);
            CHECK(lowest < hint_y);

            // Back on the settings' page the way there counts what differs from the original.
            CHECK(menu.back());
            CHECK(find_text(menu, ui::tr("setup.disasters.open", {ui::tr("setup.disasters.many", {"2"})})).has_value());
            (void)click(menu, ui::tr("setup.disasters.open", {ui::tr("setup.disasters.many", {"2"})}));
            REQUIRE(menu.disasters_page());
            (void)click(menu, ui::tr("setup.disasters.all_original"));
            CHECK(menu.setup().rules == sim::Rules{});
            CHECK(menu.back());
            CHECK(find_text(menu, ui::tr("setup.disasters.open", {ui::tr("setup.disasters.original")})).has_value());
        }
    }
    ui::set_language(ui::kFallbackLanguage);

    // A savegame's can be looked at, not changed.
    EndlessSetup saved;
    saved.rules.fires = sim::Outbreaks::Off;
    app::GameSettingsMenu menu;
    menu.open(saved, app::SetupScope::Savegame);
    (void)click(menu, ui::tr("setup.disasters.open", {ui::tr("setup.disasters.one")}));
    REQUIRE(menu.disasters_page());
    CHECK(!find_text(menu, ui::tr("setup.disasters.all_original")).has_value());
    (void)click(menu, value_of(saved, SetupItem::Fires));
    CHECK(menu.setup().rules.fires == sim::Outbreaks::Off);
}

TEST_CASE("game setup: the page of the balance changes, each on its own") {
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        for (const auto scope : {app::SetupScope::Endless, app::SetupScope::Scenario}) {
            app::GameSettingsMenu menu;
            menu.open(EndlessSetup{}, scope);
            // On the settings' page only the way there, with what is on.
            CHECK(!find_text(menu, app::setup_label(SetupItem::BalanceShips)).has_value());
            CHECK(click(menu, ui::tr("setup.balance.open", {ui::tr("setup.balance.all")})) ==
                  app::GameSettingsMenu::Result::None);
            REQUIRE(menu.balance_page());
            CHECK(find_text(menu, ui::tr("setup.title.balance")).has_value());
            CHECK(!find_text(menu, ui::tr("setup.original")).has_value());
            (void)click(menu, ui::tr("setup.balance.ships.merchant"));
            CHECK(!menu.setup().rules.large_ship_holds);
            CHECK(menu.setup().rules.revised_bread);
            (void)click(menu, ui::tr("setup.balance.hunting.wider"));
            CHECK(!menu.setup().rules.wider_hunting);

            // Every row ends above the hint.
            std::int32_t lowest = 0;
            std::int32_t hint_y = INT32_MAX;
            const std::string hint = ui::tr("setup.hint.balance");
            for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
                if (d.kind != ui::DrawItem::Kind::Text || d.text.empty()) {
                    continue;
                }
                if (d.text.size() > 20 && hint.compare(0, 20, d.text, 0, 20) == 0) {
                    hint_y = std::min(hint_y, d.y);
                } else if (d.text == app::setup_label(SetupItem::BalanceShips)) {
                    lowest = std::max(lowest, d.y);
                }
            }
            CHECK(lowest > 0);
            CHECK(lowest < hint_y);

            CHECK(menu.back());
            const std::string some = ui::tr("setup.balance.open", {ui::tr("setup.balance.some", {"1", "3"})});
            CHECK(find_text(menu, some).has_value());
            (void)click(menu, some);
            REQUIRE(menu.balance_page());
            (void)click(menu, ui::tr("setup.balance.all_original"));
            CHECK(!menu.setup().rules.revised_bread);
            CHECK(!menu.setup().rules.wider_hunting);
            CHECK(!menu.setup().rules.large_ship_holds);
            (void)click(menu, ui::tr("setup.balance.all_on"));
            CHECK(menu.setup().rules == sim::Rules{});
            (void)click(menu, ui::tr("setup.balance.all_original"));
            CHECK(menu.back());
            CHECK(find_text(menu, ui::tr("setup.balance.open", {ui::tr("setup.balance.original")})).has_value());
            // The original's settings switch every change off.
            (void)click(menu, ui::tr("setup.defaults"));
            (void)click(menu, ui::tr("setup.original"));
            CHECK(app::balance_summary(menu.setup().rules) == ui::tr("setup.balance.original"));
        }
    }
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("endless setup: a step of the refund there and back gives the same rules") {
    for (const sim::Rules& start : {sim::Rules::original(), sim::Rules::open1602()}) {
        EndlessSetup setup;
        setup.rules = start;
        for (std::size_t n = 0; n < setup_choices(SetupItem::Refund); ++n) {
            EndlessSetup s = setup;
            for (std::size_t k = 0; k < n; ++k) {
                choose_next(s, SetupItem::Refund);
            }
            for (std::size_t k = 0; k < n; ++k) {
                choose_previous(s, SetupItem::Refund);
            }
            CHECK(s.rules == start);
        }
    }
}

TEST_CASE("game settings: every setting and preset is explained in English and German") {
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < app::kSetupItems; ++i) {
        keys.emplace_back(app::setup_label_key(static_cast<SetupItem>(i)));
    }
    for (const char* key :
         {"setup.island_page", "setup.disasters", "setup.balance", "setup.victory", "setup.bugs", "setup.original",
          "setup.defaults", "setup.preview.reroll", "setup.victory.off", "setup.disasters.all_original",
          "setup.balance.all_original", "setup.balance.all_on", "setup.bugs.all_original", "setup.bugs.all_fixed"}) {
        keys.emplace_back(key);
    }
    for (const std::string_view code : {"de", "en"}) {
        ui::set_language(code);
        for (const std::string& key : keys) {
            CHECK(ui::tr(key + ".tip") != key + ".tip");
        }
    }
    ui::set_language(ui::kFallbackLanguage);
}

TEST_CASE("game settings: a setting the mouse rests on shows its explanation until it is clicked") {
    app::GameSettingsMenu menu;
    menu.open(EndlessSetup{});
    const auto at = find_text(menu, app::setup_label(SetupItem::Refund));
    REQUIRE(at.has_value());
    // Not at once: the mouse has to rest on it.
    (void)menu.mouse(at->first, at->second - 4, false);
    (void)menu.draw(kNoFonts);
    CHECK(menu.tooltip().empty());
    menu.set_tooltip_delay(std::chrono::milliseconds(0));
    bool shown = false;
    for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
        shown = shown || (d.kind == ui::DrawItem::Kind::Text && d.text.starts_with("What you get back"));
    }
    CHECK(menu.tooltip() == "setup.refund.tip");
    CHECK(shown);
    // On the value of the same row as well.
    const auto value = find_text(menu, value_of(menu.setup(), SetupItem::Refund));
    REQUIRE(value.has_value());
    (void)menu.mouse(value->first, value->second - 4, false);
    (void)menu.draw(kNoFonts);
    CHECK(menu.tooltip() == "setup.refund.tip");
    // A click hides it until the mouse goes elsewhere.
    (void)menu.mouse(value->first, value->second - 4, true);
    (void)menu.mouse(value->first, value->second - 4, false);
    (void)menu.draw(kNoFonts);
    CHECK(menu.tooltip().empty());
    const auto preset = find_text(menu, ui::tr("setup.defaults"));
    REQUIRE(preset.has_value());
    (void)menu.mouse(preset->first, preset->second - 4, false);
    (void)menu.draw(kNoFonts);
    CHECK(menu.tooltip() == "setup.defaults.tip");
    // Beside every setting, none.
    (void)menu.mouse(1000, 20, false);
    (void)menu.draw(kNoFonts);
    CHECK(menu.tooltip().empty());
}
