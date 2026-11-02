// SPDX-License-Identifier: GPL-3.0-or-later
// The scenario editor's document (ADR 0013): steps that undo and redo, the scenario's own parts in
// the savegame (SCNI, MARK, SLUA, EDIT) and the check against the original's limits.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/editor/document.hpp"
#include "o1602/editor/history.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/editor/original_check.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/scenario_data.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of the original's size with one island of 8 x 6 ground tiles.
sim::World make_world(const assets::BuildingTable& buildings) {
    sim::World world(500, 350);
    sim::IslandMap map(8, 6);
    for (std::int32_t y = 0; y < 6; ++y) {
        for (std::int32_t x = 0; x < 8; ++x) {
            REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
        }
    }
    map.capture_ground();
    assets::IslandHeader header;
    header.width = 8;
    header.height = 6;
    REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 40, 30}).ok());
    return world;
}

sim::MapCell cell(std::uint16_t sprite) {
    sim::MapCell c;
    c.sprite = sprite;
    return c;
}

/// A step that adds `amount` to the script's length, to count what the history does.
class Grow final : public editor::Operation {
public:
    Grow(std::size_t amount, std::size_t memory) : m_amount(amount), m_memory(memory) {}
    void apply(sim::World& world) override {
        world.set_scenario_script(world.scenario_script() + std::string(m_amount, 'x'));
    }
    void revert(sim::World& world) override {
        std::string s = world.scenario_script();
        s.resize(s.size() - m_amount);
        world.set_scenario_script(std::move(s));
    }
    [[nodiscard]] std::size_t memory() const noexcept override { return m_memory; }
    [[nodiscard]] std::string_view kind() const noexcept override { return "grow"; }

private:
    std::size_t m_amount;
    std::size_t m_memory;
};

}  // namespace

TEST_CASE("scenario data: language codes, marker names and texts") {
    CHECK(sim::valid_language_code("en"));
    CHECK(sim::valid_language_code("pt-BR"));
    CHECK(!sim::valid_language_code("e"));
    CHECK(!sim::valid_language_code("1en"));
    CHECK(!sim::valid_language_code("en_GB"));
    CHECK(!sim::valid_language_code(std::string(17, 'a')));

    CHECK(sim::valid_marker_name("harbour_2"));
    CHECK(!sim::valid_marker_name(""));
    CHECK(!sim::valid_marker_name("Harbour"));
    CHECK(!sim::valid_marker_name("north coast"));
    CHECK(!sim::valid_marker_name(std::string(65, 'a')));

    CHECK(
        sim::valid_scenario_text("Gr\xc3\xbc\xc3\x9f"
                                 "e \xe2\x82\xac \xf0\x9f\x8c\x8a",
                                 false));
    CHECK(sim::valid_scenario_text("one\ntwo\tthree", true));
    CHECK(!sim::valid_scenario_text("one\ntwo", false));
    CHECK(!sim::valid_scenario_text("bell\x07", true));
    CHECK(!sim::valid_scenario_text("\xfc", false));              // Windows-1252, not UTF-8
    CHECK(!sim::valid_scenario_text("\xc3", false));              // cut off
    CHECK(!sim::valid_scenario_text("\xe2\x82", false));          // cut off
    CHECK(!sim::valid_scenario_text("\xc0\xaf", false));          // overlong
    CHECK(!sim::valid_scenario_text("\xed\xa0\x80", false));      // surrogate
    CHECK(!sim::valid_scenario_text("\xf4\x90\x80\x80", false));  // beyond Unicode
    CHECK(!sim::valid_scenario_text("\xc2\x85", true));           // C1 control

    const std::vector<sim::ScenarioText> texts = {{"de", "Atoll"}, {"en", "Atoll (en)"}};
    CHECK(sim::scenario_text(texts, "de") == "Atoll");
    CHECK(sim::scenario_text(texts, "fr") == "Atoll (en)");
    CHECK(sim::scenario_text({{"de", "Nur"}}, "fr") == "Nur");
    CHECK(sim::scenario_text({}, "fr").empty());
}

TEST_CASE("scenario data: a description and markers name what is wrong") {
    sim::ScenarioInfo info;
    info.names = {{"en", "Atoll"}, {"en", "Again"}, {"x", "Bad"}};
    info.briefings = {{"de", std::string(sim::ScenarioInfo::kMaxBriefingLength + 1, 'a')}};
    info.stars = 6;
    info.author = "line\nbreak";
    const auto problems = sim::check_scenario_info(info);
    REQUIRE(problems.size() == 5);
    CHECK(problems[0] == "name: language en twice");
    CHECK(problems[1] == "name: \"x\" is no language code");
    CHECK(problems[2] == "briefing (de): longer than 16384 bytes");
    CHECK(problems[3] == "more than 5 stars");
    CHECK(problems[4] == "the author is too long, not UTF-8 or has a control character");

    using Shape = sim::ScenarioMarker::Shape;
    const std::vector<sim::ScenarioMarker> markers = {
        {"a", Shape::Point, 0, 0, 1, 1},    {"a", Shape::Point, 5, 5, 1, 1},   {"b", Shape::Point, 5, 5, 2, 1},
        {"c", Shape::Area, 90, 90, 10, 10}, {"d", Shape::Area, 90, 90, 11, 1}, {"E", Shape::Point, 0, 0, 1, 1},
        {"f", Shape::Area, 0, 0, 0, 1},
    };
    const auto wrong = sim::check_markers(markers, 100, 100);
    REQUIRE(wrong.size() == 5);
    CHECK(wrong[0] == "marker \"a\" twice");
    CHECK(wrong[1] == "marker \"b\": a point is one tile");
    CHECK(wrong[2] == "marker \"d\": outside the world");
    CHECK(wrong[3] == "marker \"E\": a name is 1 to 64 lower case letters, digits and _");
    CHECK(wrong[4] == "marker \"f\": outside the world");
}

TEST_CASE("editor history: undo and redo, a new step drops what was taken back") {
    sim::World world(100, 100);
    editor::History history;
    CHECK(!history.can_undo());
    CHECK(history.state() == 0);
    history.perform(world, std::make_unique<Grow>(1, 10));
    history.perform(world, std::make_unique<Grow>(2, 10));
    const std::uint64_t two = history.state();
    CHECK(world.scenario_script() == "xxx");
    CHECK(history.undo_steps() == 2);
    CHECK(history.next_undo() == "grow");
    CHECK(history.memory() == 20);

    CHECK(history.undo(world));
    CHECK(world.scenario_script() == "x");
    CHECK(history.redo_steps() == 1);
    CHECK(history.redo(world));
    CHECK(world.scenario_script() == "xxx");
    CHECK(history.state() == two);
    CHECK(!history.redo(world));

    CHECK(history.undo(world));
    CHECK(history.undo(world));
    CHECK(!history.undo(world));
    CHECK(world.scenario_script().empty());
    CHECK(history.state() == 0);

    history.perform(world, std::make_unique<Grow>(4, 10));
    CHECK(!history.can_redo());
    CHECK(history.memory() == 10);
    CHECK(history.state() != two);
    history.clear();
    CHECK(!history.can_undo());
    CHECK(history.memory() == 0);
}

TEST_CASE("editor history: the oldest steps go beyond the memory limit, the newest stays") {
    sim::World world(100, 100);
    editor::History history(25);
    history.perform(world, std::make_unique<Grow>(1, 10));
    history.perform(world, std::make_unique<Grow>(1, 10));
    history.perform(world, std::make_unique<Grow>(1, 10));
    CHECK(history.undo_steps() == 2);
    CHECK(history.memory() == 20);
    const std::uint64_t three = history.state();
    CHECK(history.undo(world));
    CHECK(history.undo(world));
    CHECK(!history.undo(world));
    CHECK(world.scenario_script() == "x");  // the first step can no longer be taken back
    CHECK(history.state() != 0);            // and the state is not that of no step
    CHECK(history.redo(world));
    CHECK(history.redo(world));
    CHECK(history.state() == three);

    history.perform(world, std::make_unique<Grow>(1, 100));  // larger than the limit alone
    CHECK(history.undo_steps() == 1);
    CHECK(history.memory() == 100);
}

TEST_CASE("editor operations: tile edits collect changes and take them back in order") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    const sim::MapCell original = world.islands()[0].map.top(2, 3);

    editor::TileEditBuilder builder(world);
    CHECK(builder.set(0, editor::Layer::Top, 2, 3, cell(100)));
    CHECK(builder.set(0, editor::Layer::Top, 2, 3, cell(101)));  // the same tile again
    CHECK(builder.set(0, editor::Layer::Ground, 1, 1, cell(102)));
    CHECK(builder.set(0, editor::Layer::Top, 4, 4, world.islands()[0].map.top(4, 4)));  // no change
    CHECK(!builder.set(0, editor::Layer::Top, 8, 0, cell(103)));                        // outside the island
    CHECK(!builder.set(1, editor::Layer::Top, 0, 0, cell(103)));                        // no such island
    CHECK(builder.cell(0, editor::Layer::Top, 2, 3)->sprite == 101);
    CHECK(builder.cell(0, editor::Layer::Top, 0, 0)->sprite == original.sprite);
    CHECK(!builder.cell(0, editor::Layer::Top, 0, 6));

    auto edit = builder.finish();
    REQUIRE(edit != nullptr);
    REQUIRE(edit->changes().size() == 2);
    CHECK(edit->changes()[0].before.sprite == original.sprite);
    CHECK(edit->changes()[0].after.sprite == 101);

    editor::Document document(std::move(world));
    CHECK(!document.modified());
    document.perform(std::move(edit));
    CHECK(document.modified());
    CHECK(document.world().islands()[0].map.top(2, 3).sprite == 101);
    CHECK(document.world().islands()[0].map.ground(1, 1).sprite == 102);
    CHECK(document.history().next_undo() == "tiles");
    CHECK(document.undo());
    CHECK(!document.modified());
    CHECK(document.world().islands()[0].map.top(2, 3).sprite == original.sprite);

    editor::TileEditBuilder unchanged(document.world());
    CHECK(unchanged.set(0, editor::Layer::Top, 0, 0, document.world().islands()[0].map.top(0, 0)));
    CHECK(unchanged.finish() == nullptr);
}

TEST_CASE("editor document: a scenario is saved, opened and keeps its own parts") {
    const auto buildings = make_buildings();
    editor::Document document(make_world(buildings));
    sim::ScenarioInfo info;
    info.names = {{"en", "Two Coasts"}, {"de", "Zwei K\xc3\xbcsten"}};
    info.briefings = {{"en", "Found a city.\nKeep it fed."}};
    info.stars = 3;
    info.author = "Merlin";
    info.version = "1.0";
    document.perform(std::make_unique<editor::ScenarioInfoEdit>(std::nullopt, info));
    const std::vector<sim::ScenarioMarker> markers = {
        {"harbour", sim::ScenarioMarker::Shape::Point, 41, 32, 1, 1},
        {"coast", sim::ScenarioMarker::Shape::Area, 40, 30, 8, 6},
    };
    document.perform(std::make_unique<editor::MarkersEdit>(std::vector<sim::ScenarioMarker>{}, markers));
    document.perform(std::make_unique<editor::ScriptEdit>("", "function on_start() end\n"));
    CHECK(document.modified());

    auto bytes = document.save(buildings);
    REQUIRE(bytes.ok());
    CHECK(!document.modified());
    CHECK(document.history().undo_steps() == 3);  // a scenario already: no step added
    CHECK(document.undo());
    CHECK(document.modified());
    CHECK(document.redo());
    CHECK(!document.modified());

    auto opened = editor::Document::open(*bytes, buildings, nullptr);
    REQUIRE(opened.ok());
    CHECK(!opened->modified());
    CHECK(opened->world().scenario_info() == info);
    CHECK(opened->world().markers() == markers);
    CHECK(opened->world().scenario_script() == "function on_start() end\n");
    auto again = opened->save(buildings);
    REQUIRE(again.ok());
    CHECK(*again == *bytes);  // the same scenario gives the same bytes

    // The list of scenarios reads the description from the head of the file alone.
    auto head = sim::save_info(*bytes);
    REQUIRE(head.ok());
    REQUIRE(head->meta.has_value());
    REQUIRE(head->meta->scenario.has_value());
    CHECK(head->meta->scenario->stars == 3);
    CHECK(head->meta->scenario->names == info.names);
    CHECK(!head->meta->scenario->original_limits);

    // A file of the original needs the game's directory.
    const std::vector<std::uint8_t> chunks = {'I', 'N', 'S', 'E', 'L', '5'};
    CHECK(!editor::Document::open(chunks, buildings, nullptr).ok());
}

TEST_CASE("editor document: a world that is no scenario gets a description as one step") {
    const auto buildings = make_buildings();
    editor::Document document(make_world(buildings));
    CHECK(!document.world().scenario_info());
    auto bytes = document.save(buildings);
    REQUIRE(bytes.ok());
    REQUIRE(document.world().scenario_info().has_value());
    CHECK(document.history().next_undo() == "scenario");
    CHECK(!document.modified());
    CHECK(document.undo());
    CHECK(!document.world().scenario_info());
    CHECK(document.modified());

    // A savegame without a description has no scenario head.
    auto plain = sim::write_game(make_world(buildings), buildings);
    REQUIRE(plain.ok());
    auto head = sim::save_info(*plain);
    REQUIRE(head.ok());
    REQUIRE(head->meta.has_value());
    CHECK(!head->meta->scenario.has_value());
}

TEST_CASE("editor document: wrong scenario parts refuse the save and change nothing") {
    const auto buildings = make_buildings();
    editor::Document document(make_world(buildings));
    document.perform(std::make_unique<editor::MarkersEdit>(
        std::vector<sim::ScenarioMarker>{},
        std::vector<sim::ScenarioMarker>{{"far", sim::ScenarioMarker::Shape::Point, 500, 0, 1, 1}}));
    const std::size_t steps = document.history().undo_steps();
    auto bytes = document.save(buildings);
    REQUIRE(!bytes.ok());
    CHECK(bytes.error().message == "scenario: marker \"far\": outside the world");
    CHECK(document.history().undo_steps() == steps);
    CHECK(!document.world().scenario_info());
    CHECK(document.modified());
    REQUIRE(document.check_scenario().size() == 1);
}

TEST_CASE("editor original limits: a world of the original's size passes, more is named") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings);
    CHECK(editor::check_original_limits(world).empty());

    world.set_scenario_script("print(1)");
    sim::World large(600, 350);
    large.set_scenario_script("print(1)");
    const auto problems = editor::check_original_limits(large);
    REQUIRE(problems.size() == 2);
    CHECK(problems[0] == "the map is 600 x 350 tiles, the original's is 500 x 350");
    CHECK(problems[1] == "a script");
    REQUIRE(editor::check_original_limits(world).size() == 1);

    std::vector<sim::Player> players(6);
    for (sim::Player& p : players) {
        p.role = sim::PlayerRole::Computer;
    }
    players[5].role = sim::PlayerRole::Neutral;
    world.set_players(players);
    const auto many = editor::check_original_limits(world);
    REQUIRE(many.size() == 2);
    CHECK(many[0] == "5 players, the original holds 4");
}

TEST_CASE("editor document: edits a world kept elsewhere, which stays where it is when the document moves") {
    const auto buildings = make_buildings();
    sim::World shown = make_world(buildings);
    std::optional<editor::Document> document;
    document.emplace(editor::Document::edit(shown));
    CHECK(&document->world() == &shown);
    CHECK(!document->modified());
    document->perform(std::make_unique<editor::ScriptEdit>("", "print('hi')\n"));
    CHECK(shown.scenario_script() == "print('hi')\n");  // the world shown changed, not a copy

    editor::Document moved = std::move(*document);
    document.reset();
    CHECK(&moved.world() == &shown);
    CHECK(moved.undo());
    CHECK(shown.scenario_script().empty());
    CHECK(!moved.modified());

    // Saved, read back as the editor reads files: the same world.
    auto bytes = moved.save(buildings);
    REQUIRE(bytes.ok());
    auto read = editor::Document::read(*bytes, buildings, nullptr);
    REQUIRE(read.ok());
    CHECK(read->islands().size() == 1);
    CHECK(read->scenario_info().has_value());
}
