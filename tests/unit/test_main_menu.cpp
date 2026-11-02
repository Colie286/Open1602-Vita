// SPDX-License-Identifier: GPL-3.0-or-later
// The main menu and the screens before a game (BT-1) without the original data: a folder with the
// four files an installation needs, synthetic BASE, HOST, MISSION and FARBWAHL layouts with one
// gadget per button the menu knows, a few scenario files and the GAME texts of the list headings.

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "main_menu.hpp"

#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"

using namespace o1602;
using app::MainMenu;
using app::MenuEvent;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};
constexpr std::int32_t kWidth = 20;
constexpr std::int32_t kHeight = 10;

/// The gadgets of a synthetic layout: each a centred text box of its own place.
struct Layout {
    std::vector<std::int32_t> ids;
    bool column = false;  ///< one below the other, as the buttons of BASE.GAD

    [[nodiscard]] std::string gad() const {
        std::string out = "Objekt: GADGET\n";
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const auto [x, y] = place(i);
            out += "  @Nummer: " + std::to_string(i) + "\n  Id: " + std::to_string(ids[i]) +
                   "\n  Kind: 6\n  Pos: " + std::to_string(x) + ", " + std::to_string(y) +
                   "\n  Size: " + std::to_string(kWidth) + ", " + std::to_string(kHeight) + "\n";
        }
        return out + "EndObj\n";
    }

    [[nodiscard]] std::pair<std::int32_t, std::int32_t> place(std::size_t i) const {
        if (column) {
            return {100, 100 + static_cast<std::int32_t>(i) * (kHeight + 5)};
        }
        return {static_cast<std::int32_t>(i % 20) * (kWidth + 10), 100 + static_cast<std::int32_t>(i / 20) * 20};
    }

    [[nodiscard]] std::pair<std::int32_t, std::int32_t> centre(std::int32_t id) const {
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) {
                const auto [x, y] = place(i);
                return {x + kWidth / 2, y + kHeight / 2};
            }
        }
        REQUIRE(false);
        return {};
    }
};

std::vector<std::int32_t> range(std::int32_t first, std::int32_t count) {
    std::vector<std::int32_t> out;
    for (std::int32_t k = 0; k < count; ++k) {
        out.push_back(first + k);
    }
    return out;
}

Layout base_layout() {
    return {{30002, 30003, 30004, 30005, 30006}};
}

/// The History Edition's BASE.GAD: Options (30004) after Multiplayer, Quit at 30007.
Layout history_base_layout() {
    return {{30002, 30003, 30004, 30005, 30006, 30007}, true};
}

Layout host_layout() {
    Layout l{{31002, 31003, 31004, 31005, 31011, 31012}};
    for (const std::int32_t first : {31021, 31041, 31061}) {
        const auto rows = range(first, 14);
        l.ids.insert(l.ids.end(), rows.begin(), rows.end());
    }
    return l;
}

Layout mission_layout() {
    Layout l{{32001, 32002, 32004, 32005, 32006, 32007, 32031, 32041, 32091, 32092, 32093, 32094}};
    for (const auto& [first, count] :
         {std::pair{32011, 5}, {32021, 5}, {32051, 4}, {32061, 4}, {32071, 4}, {32081, 4}}) {
        const auto ids = range(first, count);
        l.ids.insert(l.ids.end(), ids.begin(), ids.end());
    }
    return l;
}

Layout colour_layout() {
    Layout l{{35002, 35003}};
    for (const std::int32_t first : {35011, 35021}) {
        const auto ids = range(first, 4);
        l.ids.insert(l.ids.end(), ids.begin(), ids.end());
    }
    return l;
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

/// An installation with the menu's layouts and the scenarios `scenarios` (file names in Szenes).
struct Game {
    std::filesystem::path root;
    std::optional<assets::GameDirectory> dir;
    assets::TextTable texts;

    Game(const std::string& name, const std::vector<std::string>& scenarios, const Layout& base = base_layout()) {
        root = test::temp_path(name);
        std::filesystem::remove_all(root);
        for (const char* file : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            write(root / file, "");
        }
        write(root / "GADDATA" / "BASE.GAD", base.gad());
        write(root / "GADDATA" / "HOST.GAD", host_layout().gad());
        write(root / "GADDATA" / "MISSION.GAD", mission_layout().gad());
        write(root / "GADDATA" / "FARBWAHL.GAD", colour_layout().gad());
        for (const std::string& s : scenarios) {
            if (s.starts_with("Endlos")) {
                // The endless game: mission number 1 in its header.
                assets::ChunkWriter w;
                const std::array<std::uint8_t, 4> endless = {1, 0, 0, 0};
                w.add("SZENE_MISSNR", endless);
                const core::Bytes& b = w.bytes();
                write(root / "Szenes" / s, std::string(b.begin(), b.end()));
            } else {
                write(root / "Szenes" / s, "not a scenario");  // no header: an adventure without stars
            }
        }
        auto opened = assets::GameDirectory::open(root);
        REQUIRE(opened.ok());
        dir = std::move(opened).value();
        std::string game = "[GAME]\n";
        for (int i = 0; i < 89; ++i) {
            game += i == 87 ? "----Abenteuer----\n" : "Text " + std::to_string(i) + "\n";
        }
        texts = assets::TextTable::parse(game);
    }
    ~Game() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;
};

std::vector<MenuEvent> click(MainMenu& menu, const Layout& layout, std::int32_t id) {
    const auto [x, y] = layout.centre(id);
    auto events = menu.mouse(x, y, true);
    for (MenuEvent& e : menu.mouse(x, y, false)) {
        events.push_back(std::move(e));
    }
    return events;
}

/// The texts the screen shows, in the order of its gadgets.
std::vector<std::string> texts(MainMenu& menu) {
    std::vector<std::string> out;
    for (const ui::DrawItem& d : menu.draw(kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && !d.text.empty()) {
            out.push_back(d.text);
        }
    }
    return out;
}

bool shows(MainMenu& menu, const std::string& text) {
    const auto all = texts(menu);
    return std::find(all.begin(), all.end(), text) != all.end();
}

}  // namespace

TEST_CASE("main menu: the buttons of the main menu answer with their events") {
    Game g("o1602-test-menu-base", {});
    MainMenu menu(*g.dir, &g.texts);
    REQUIRE(menu.usable());
    CHECK(menu.screen() == MainMenu::Screen::Base);
    const auto one = [&](std::int32_t id) {
        const auto events = click(menu, base_layout(), id);
        REQUIRE(events.size() == 1);
        return events[0].kind;
    };
    CHECK(one(30003) == MenuEvent::Kind::Multiplayer);
    CHECK(one(30004) == MenuEvent::Kind::Credits);
    CHECK(one(30005) == MenuEvent::Kind::Intro);
    CHECK(one(30006) == MenuEvent::Kind::Quit);
    CHECK(menu.screen() == MainMenu::Screen::Base);
    CHECK(!menu.back());  // Escape in the main menu: nothing to go back to
}

TEST_CASE("main menu: the History Edition's Options button opens the window Anzeige, the others count one further") {
    Game g("o1602-test-menu-history-base", {}, history_base_layout());
    MainMenu menu(*g.dir, &g.texts);
    REQUIRE(menu.usable());
    const auto one = [&](std::int32_t id) {
        const auto events = click(menu, history_base_layout(), id);
        REQUIRE(events.size() == 1);
        return events[0].kind;
    };
    CHECK(one(30003) == MenuEvent::Kind::Multiplayer);
    CHECK(one(30004) == MenuEvent::Kind::Display);
    CHECK(one(30005) == MenuEvent::Kind::Credits);
    CHECK(one(30006) == MenuEvent::Kind::Intro);
    CHECK(one(30007) == MenuEvent::Kind::Quit);
    CHECK(menu.screen() == MainMenu::Screen::Base);
}

TEST_CASE("main menu: the list of scenarios, a mission screen with its parts, the rules before the start") {
    Game g("o1602-test-menu-list", {"Insel0.szs", "Insel1.szs", "Riff.szs"});
    MainMenu menu(*g.dir, &g.texts);
    CHECK(click(menu, base_layout(), 30002).empty());
    REQUIRE(menu.screen() == MainMenu::Screen::Host);
    // One entry per name without its digits, under the heading of the adventures.
    const auto list = texts(menu);
    REQUIRE(list.size() >= 3);
    CHECK(list[0] == "----Abenteuer----");
    CHECK(list[1] == "Insel");
    CHECK(list[2] == "Riff");
    // The heading is not a scenario.
    CHECK(click(menu, host_layout(), 31021).empty());
    CHECK(menu.screen() == MainMenu::Screen::Host);
    // Insel: a mission screen with two parts, the first chosen.
    click(menu, host_layout(), 31022);
    REQUIRE(menu.screen() == MainMenu::Screen::Mission);
    const auto parts = texts(menu);
    CHECK(std::count_if(parts.begin(), parts.end(),
                        [](const std::string& t) { return t.find('1') != std::string::npos; }) >= 1);
    CHECK(std::count_if(parts.begin(), parts.end(),
                        [](const std::string& t) { return t.find('2') != std::string::npos; }) >= 1);
    click(menu, mission_layout(), 32012);  // the second part
    // Start: the rules first (Open1602's own dialog), Escape back to the mission.
    CHECK(click(menu, mission_layout(), 32001).empty());
    CHECK(menu.screen() == MainMenu::Screen::Settings);
    CHECK(menu.back());
    CHECK(menu.screen() == MainMenu::Screen::Mission);
    click(menu, mission_layout(), 32002);  // cancel
    CHECK(menu.screen() == MainMenu::Screen::Host);
    CHECK(menu.back());
    CHECK(menu.screen() == MainMenu::Screen::Base);
    // "Neues Spiel" on the end screen finds the scenario again by its name.
    CHECK(menu.open_mission("insel"));
    CHECK(menu.screen() == MainMenu::Screen::Mission);
    CHECK(!menu.open_mission("Nowhere"));
    CHECK(menu.screen() == MainMenu::Screen::Base);
}

TEST_CASE("main menu: a campaign takes the header of its last part, whatever order the files come in") {
    // Pirata1 and Feindliche Nachbarn1 say campaign 0 where their other parts say 3 and 4 (D-015);
    // the directory order of a file system must not let such a part decide the whole entry.
    const std::filesystem::path folder = test::temp_path("o1602-test-menu-campaign");
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto scenario = [&](const char* file, std::uint8_t campaign, std::uint8_t part) {
        assets::ChunkWriter w;
        const std::array<std::uint8_t, 4> c = {campaign, 0, 0, 0};
        const std::array<std::uint8_t, 4> r = {part, 0, 0, 0};
        w.add("SZENE_KAMPAGNE", c);
        w.add("SZENE_RANKING", r);
        const core::Bytes& b = w.bytes();
        std::ofstream(folder / file, std::ios::binary)
            .write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    };
    scenario("Pirata2.szs", 3, 2);
    scenario("Pirata1.szs", 0, 2);  // written last, so a file system that lists by age gives it last
    scenario("Pirata0.szs", 3, 4);
    const std::array<std::string, 4> headings = {"m", "k", "a", "e"};
    const std::vector<app::MenuScenario> list = app::scenario_list(folder, headings, nullptr);
    const auto pirata =
        std::find_if(list.begin(), list.end(), [](const app::MenuScenario& s) { return s.name == "Pirata"; });
    REQUIRE(pirata != list.end());
    CHECK(pirata->header.campaign == 3);
    CHECK(pirata->header.ranking == 2);  // the stars of the last part, as the original shows them
    CHECK(pirata->files.size() == 3);
    // A won part counts for the entry's campaign and the digit of its name, not its own header.
    const auto part = app::campaign_part_of(folder / "Pirata1.szs");
    REQUIRE(part.has_value());
    CHECK(part->campaign == 3);
    CHECK(part->part == 1);
    CHECK(!app::campaign_part_of(folder / "game11.gam").has_value());
    std::filesystem::remove_all(folder);
}

TEST_CASE("main menu: the endless game lists what sets its chosen level apart") {
    Game g("o1602-test-menu-endless", {"Endlos0.szs", "Endlos1.szs", "Endlos2.szs", "Endlos3.szs"});
    MainMenu menu(*g.dir, &g.texts);
    click(menu, base_layout(), 30002);
    REQUIRE(menu.screen() == MainMenu::Screen::Host);
    const auto list = texts(menu);
    const auto row = std::find(list.begin(), list.end(), "Endlos");
    REQUIRE(row != list.end());
    click(menu, host_layout(), 31021 + static_cast<std::int32_t>(row - list.begin()));
    REQUIRE(menu.screen() == MainMenu::Screen::Mission);
    const auto briefing_with = [&](const std::string& line) {
        const auto all = texts(menu);
        return std::any_of(all.begin(), all.end(),
                           [&](const std::string& t) { return t.find(line) != std::string::npos; });
    };
    CHECK(briefing_with("* no pirates"));
    CHECK(!briefing_with("* frequent droughts"));
    click(menu, mission_layout(), 32014);  // the fourth level
    CHECK(briefing_with("* frequent droughts"));
    CHECK(!briefing_with("* no pirates"));
}

TEST_CASE("main menu: a long list scrolls by fourteen rows") {
    std::vector<std::string> files;
    for (char c = 'A'; c <= 'T'; ++c) {
        files.push_back(std::string("Karte") + c + ".szs");
    }
    Game g("o1602-test-menu-scroll", files);
    MainMenu menu(*g.dir, &g.texts);
    click(menu, base_layout(), 30002);
    REQUIRE(menu.screen() == MainMenu::Screen::Host);
    CHECK(shows(menu, "KarteA"));
    CHECK(!shows(menu, "KarteT"));  // heading and 20 scenarios: 21 entries, 14 rows
    click(menu, host_layout(), 31012);
    CHECK(shows(menu, "KarteT"));
    CHECK(!shows(menu, "KarteA"));
    click(menu, host_layout(), 31011);
    CHECK(shows(menu, "KarteA"));
}

TEST_CASE("main menu: the mouse wheel scrolls the scenarios and the savegames a row per notch") {
    std::vector<std::string> files;
    for (char c = 'A'; c <= 'T'; ++c) {
        files.push_back(std::string("Karte") + c + ".szs");
    }
    Game g("o1602-test-menu-wheel", files);
    MainMenu menu(*g.dir, &g.texts);
    CHECK(!menu.wheel(-1));  // the main menu has no list
    click(menu, base_layout(), 30002);
    REQUIRE(menu.screen() == MainMenu::Screen::Host);
    CHECK(shows(menu, "KarteA"));
    CHECK(menu.wheel(-2));  // the heading and KarteA leave the top
    CHECK(!shows(menu, "KarteA"));
    CHECK(shows(menu, "KarteB"));
    CHECK(!shows(menu, "KarteP"));
    CHECK(menu.wheel(-1));
    CHECK(shows(menu, "KarteP"));
    CHECK(menu.wheel(-50));  // not past the end: 21 entries, the last 14 rows
    CHECK(shows(menu, "KarteT"));
    CHECK(shows(menu, "KarteG"));
    CHECK(!shows(menu, "KarteF"));
    CHECK(menu.wheel(50));  // up, not before the first
    CHECK(shows(menu, "KarteA"));

    app::SaveSlots slots;
    for (std::size_t i = 0; i < 20; ++i) {
        slots.set(i, "Spiel " + std::to_string(i), 1);
    }
    menu.set_slots(slots);
    click(menu, host_layout(), 31003);  // "Spiel laden"
    CHECK(shows(menu, "Spiel 0"));
    CHECK(menu.wheel(-3));
    CHECK(!shows(menu, "Spiel 2"));
    CHECK(shows(menu, "Spiel 3"));
}

TEST_CASE("main menu: the savegames to load, and the last game to continue") {
    Game g("o1602-test-menu-load", {});
    MainMenu menu(*g.dir, &g.texts);
    app::SaveSlots slots;
    slots.set(1, "Mein Spiel", 1);
    menu.set_slots(slots);
    click(menu, base_layout(), 30002);
    click(menu, host_layout(), 31003);  // "Spiel laden"
    CHECK(shows(menu, "Mein Spiel"));
    CHECK(click(menu, host_layout(), 31021).empty());  // a free slot loads nothing
    const auto load = click(menu, host_layout(), 31022);
    REQUIRE(load.size() == 1);
    CHECK(load[0].kind == MenuEvent::Kind::LoadSlot);
    CHECK(load[0].slot == 1);
    // The first row is the last game, loaded like the others.
    slots.set(app::kLastGameSlot, std::string(app::kLastGameName), 1);
    menu.set_slots(slots);
    click(menu, host_layout(), 31003);
    CHECK(shows(menu, "lastgame"));
    const auto first = click(menu, host_layout(), 31021);
    REQUIRE(first.size() == 1);
    CHECK(first[0].kind == MenuEvent::Kind::LoadSlot);
    CHECK(first[0].slot == app::kLastGameSlot);
    const auto last = click(menu, host_layout(), 31004);
    REQUIRE(last.size() == 1);
    CHECK(last[0].kind == MenuEvent::Kind::ContinueLast);
    click(menu, host_layout(), 31005);  // back to the main menu
    CHECK(menu.screen() == MainMenu::Screen::Base);
}

TEST_CASE("main menu: without its layouts the menu is not usable") {
    Game g("o1602-test-menu-missing", {});
    std::filesystem::remove(g.root / "GADDATA" / "HOST.GAD");
    MainMenu menu(*g.dir, &g.texts);
    CHECK(!menu.usable());
}

TEST_CASE("main menu: the screen \"Mods\" turns packages on and off and takes in received ones") {
    Game g("o1602-test-menu-mods", {});
    MainMenu menu(*g.dir, &g.texts);
    REQUIRE(menu.usable());
    const auto press = [&](std::int32_t x, std::int32_t y) {
        auto events = menu.mouse(x, y, true);
        for (MenuEvent& e : menu.mouse(x, y, false)) {
            events.push_back(std::move(e));
        }
        return events;
    };
    // Open1602's own button below the original's.
    auto opened = press(104, 733);
    REQUIRE(opened.size() == 1);
    CHECK(opened[0].kind == MenuEvent::Kind::Mods);
    CHECK(menu.screen() == MainMenu::Screen::Mods);

    app::ModsModel model;
    app::ModsModel::Entry own;
    own.id = "leuchtturm";
    own.folder = "mods/leuchtturm";
    own.name = "Leuchtturm";
    own.version = "1.2";
    app::ModsModel::Entry received = own;
    received.id = "wetter";
    received.folder = "mods-cache/wetter-0123456789abcdef";
    received.name = "Wetter";
    received.state = app::ModsModel::Entry::State::Received;
    received.adoptable = true;
    app::ModsModel::Entry broken = own;
    broken.id = "nebel";
    broken.folder = "mods/nebel";
    broken.name = "Nebel";
    broken.state = app::ModsModel::Entry::State::Broken;
    const assets::PackageProblem why{"nebel", assets::PackageProblem::Kind::Missing, "wetter", ">=2", {}};
    broken.short_problem = app::ModsMenu::short_problem_text(why);
    broken.problem = app::ModsMenu::problem_text(why);
    model.entries = {own, received, broken};
    menu.set_mods(model);
    CHECK(shows(menu, "Leuchtturm"));
    CHECK(shows(menu, broken.short_problem));
    CHECK(broken.short_problem.find("wetter") != std::string::npos);
    CHECK(broken.problem.find("wetter >=2") != std::string::npos);
    CHECK(shows(menu, app::ModsMenu::state_text(app::ModsModel::Entry::State::Received)));

    auto toggled = press(84, 203);  // the check box of the first row
    REQUIRE(toggled.size() == 1);
    CHECK(toggled[0].kind == MenuEvent::Kind::ToggleMod);
    CHECK(toggled[0].name == "leuchtturm");

    CHECK(press(300, 237).empty());  // chooses the received package
    auto adopted = press(200, 686);
    REQUIRE(adopted.size() == 1);
    CHECK(adopted[0].kind == MenuEvent::Kind::AdoptMod);
    CHECK(adopted[0].path == received.folder);

    CHECK(menu.back());
    CHECK(menu.screen() == MainMenu::Screen::Base);
}

TEST_CASE("main menu: campaigns by stars then number, each by the header of its last file (0x0048E710)") {
    const std::filesystem::path folder = test::temp_path("o1602-test-menu-order");
    std::filesystem::remove_all(folder);
    const auto scenario = [&](const std::string& file, std::uint32_t campaign, std::uint32_t ranking) {
        assets::ChunkWriter w;
        const auto word = [](std::uint32_t v) {
            return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(v), 0, 0, 0};
        };
        if (campaign != assets::ScenarioHeader::kNone) {
            w.add("SZENE_KAMPAGNE", word(campaign));
        }
        w.add("SZENE_RANKING", word(ranking));
        const core::Bytes& b = w.bytes();
        write(folder / file, std::string(b.begin(), b.end()));
    };
    scenario("Alpha0.szs", 1, 1);
    scenario("Alpha1.szs", 1, 1);
    scenario("Zeta0.szs", assets::ScenarioHeader::kNone, 3);  // only the last file counts
    scenario("Zeta1.szs", 2, 0);
    scenario("Beta0.szs", 3, 0);
    const std::array<std::string, 4> headings = {"Original", "Kampagnen", "Abenteuer", "Eigene"};
    const auto list = app::scenario_list(folder, headings, nullptr);
    std::vector<std::string> names;
    for (const app::MenuScenario& s : list) {
        names.push_back(s.name);
    }
    CHECK((names == std::vector<std::string>{"Kampagnen", "Zeta", "Beta", "Alpha"}));
    std::filesystem::remove_all(folder);
}

TEST_CASE("main menu: the own scenario folder under its heading, in name order, without names listed already") {
    CHECK(app::own_scenario_list({}, "Own", {}).empty());
    const std::filesystem::path folder = test::temp_path("o1602-test-menu-own");
    std::filesystem::remove_all(folder);
    CHECK(app::own_scenario_list(folder, "Own", {}).empty());  // a missing folder lists nothing
    const auto scenario = [&](const std::string& file, std::uint32_t ranking) {
        assets::ChunkWriter w;
        const std::array<std::uint8_t, 4> r = {static_cast<std::uint8_t>(ranking), 0, 0, 0};
        w.add("SZENE_RANKING", r);
        const core::Bytes& b = w.bytes();
        write(folder / file, std::string(b.begin(), b.end()));
    };
    scenario("Zwei Inseln0.szs", 1);
    scenario("Zwei Inseln1.szs", 3);
    scenario("Grand Sandbox.szs", 4);
    scenario("Das Atoll.szs", 2);  // the game folder has it: left out
    write(folder / "Eigene Karte.o1602", "O1602");
    write(folder / "Grand Sandbox.o1602", "O1602");  // named like the .szs: left out
    write(folder / "notes.txt", "not a scenario");
    std::vector<app::MenuScenario> taken(2);
    taken[0].name = "Original";
    taken[0].separator = true;
    taken[1].name = "Das Atoll";
    std::vector<std::string> skipped;
    const auto list = app::own_scenario_list(folder, "Own", taken, &skipped);
    std::vector<std::string> names;
    for (const app::MenuScenario& s : list) {
        names.push_back(s.name);
    }
    CHECK((names == std::vector<std::string>{"Own", "Eigene Karte", "Grand Sandbox", "Zwei Inseln"}));
    REQUIRE(list.size() == 4);
    CHECK(list[0].separator);
    CHECK(list[1].files.size() == 1);
    CHECK(list[1].files[0].first == 0xFF);
    CHECK(list[1].files[0].second.extension() == ".o1602");
    CHECK(list[2].header.ranking == 4);
    CHECK(list[2].files[0].second.extension() == ".szs");
    CHECK(list[3].files.size() == 2);  // the parts of one name make one entry, as in the game folder
    CHECK(list[3].header.ranking == 3);
    std::sort(skipped.begin(), skipped.end());
    CHECK((skipped == std::vector<std::string>{"Das Atoll", "Grand Sandbox"}));
    std::filesystem::remove_all(folder);
}

TEST_CASE("main menu: the packages' scenarios under their own heading, with stars and briefing") {
    CHECK(app::package_scenario_list({}, {}, "en", "----Mods----").empty());
    std::vector<assets::PackageScenario> scenarios(1);
    assets::PackageScenario& s = scenarios[0];
    s.id = "isles:north";
    s.package = "isles";
    s.file = std::filesystem::path("mods") / "isles" / "north.szs";
    s.stars = 4;
    s.names.emplace_back("en", "The North");
    s.names.emplace_back("de", "Der Norden");
    s.briefings.emplace_back("de", "Siedelt im Norden.");
    const auto list = app::package_scenario_list(scenarios, {}, "de", "----Mods----");
    REQUIRE(list.size() == 2);
    CHECK(list[0].separator);
    CHECK(list[0].name == "----Mods----");
    CHECK(!list[1].separator);
    CHECK(list[1].name == "Der Norden");
    CHECK(list[1].base == std::filesystem::path("isles_north"));
    REQUIRE(list[1].files.size() == 1);
    CHECK(list[1].files[0].first == 0xFF);
    CHECK(list[1].files[0].second == s.file);
    CHECK(list[1].header.ranking == 4);
    CHECK(list[1].package_scenario == "isles:north");
    CHECK(list[1].briefing == "Siedelt im Norden.");
}

TEST_CASE("main menu: a package's campaign is one entry with a part per scenario") {
    std::vector<assets::PackageScenario> scenarios(2);
    scenarios[0].id = "isles:north";
    scenarios[0].file = std::filesystem::path("north.szs");
    scenarios[0].briefings.emplace_back("en", "Go north.");
    scenarios[1].id = "isles:south";
    scenarios[1].file = std::filesystem::path("south.szs");
    std::vector<assets::PackageCampaign> campaigns(1);
    campaigns[0].id = "isles:frost";
    campaigns[0].names.emplace_back("en", "Frost");
    campaigns[0].briefings.emplace_back("en", "Two winters.");
    campaigns[0].stars = 3;
    campaigns[0].parts = {"isles:north", "isles:south"};
    const auto list = app::package_scenario_list(scenarios, campaigns, "en", "----Mods----");
    REQUIRE(list.size() == 4);  // heading, campaign, two scenarios
    const app::MenuScenario& c = list[1];
    CHECK(c.name == "Frost");
    CHECK(c.package_campaign == "isles:frost");
    CHECK(c.package_scenario.empty());
    CHECK(c.briefing == "Two winters.");
    CHECK(c.header.ranking == 3);
    CHECK(c.base == std::filesystem::path("isles_frost"));
    REQUIRE(c.files.size() == 2);
    CHECK(c.files[0].first == 0);
    CHECK(c.files[1].first == 1);
    CHECK(c.files[1].second == std::filesystem::path("south.szs"));
    REQUIRE(c.package_parts.size() == 2);
    CHECK(c.package_parts[0].scenario == "isles:north");
    CHECK(c.package_parts[0].briefing == "Go north.");
    CHECK(c.package_parts[1].briefing.empty());
    CHECK(list[2].package_scenario == "isles:north");
}
