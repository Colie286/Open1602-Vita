// SPDX-License-Identifier: GPL-3.0-or-later
// The settings of the side panel (BT-2) without the original data: synthetic OPTION, DISK, MUSIK
// and ANNO layouts with one gadget per Id, clicked with the mouse as the side panel does.

#include <algorithm>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "options_panel.hpp"

#include "o1602/assets/gad.hpp"
#include "o1602/assets/text_table.hpp"

using namespace o1602;
using app::OptionsEvent;
using app::OptionsModel;
using app::OptionsPanel;

namespace {

/// A layout of the Ids first..last but `missing`, each a 20x10 box of its own place; `sliders`
/// slide across.
assets::GadgetLayout layout(std::int32_t first, std::int32_t last, const std::vector<std::int32_t>& missing = {},
                            const std::vector<std::int32_t>& sliders = {}) {
    std::string text = "Objekt: GADGET\n";
    for (std::int32_t id = first; id <= last; ++id) {
        if (std::find(missing.begin(), missing.end(), id) != missing.end()) {
            continue;
        }
        const std::int32_t i = id - first;
        text += "  @Nummer: " + std::to_string(i) + "\n  Id: " + std::to_string(id) +
                "\n  Kind: 6\n  Pos: " + std::to_string(i % 8 * 30) + ", " + std::to_string(i / 8 * 20) +
                "\n  Size: 20, 10\n";
        if (std::find(sliders.begin(), sliders.end(), id) != sliders.end()) {
            text += "  Slidhorflg: 1\n";
        }
    }
    return assets::parse_gad(text + "EndObj\n");
}

/// DISK.GAD: the original's twelve savegame rows, or the History Edition's ten with a button for a
/// new savegame (48014) and arrows of its own (48016, 48017).
assets::GadgetLayout disk_layout(bool history) {
    if (history) {
        return layout(48002, 48080, {48031, 48032, 48051, 48052, 48071, 48072});
    }
    return layout(48002, 48080, {48014, 48016, 48017});
}

struct Options {
    assets::TextTable texts = assets::TextTable::parse("[GAME]\n" + std::string(40, '\n'));
    OptionsPanel panel;

    /// The History Edition's OPTION.GAD has a button for its own settings screen (47016).
    explicit Options(bool history = false)
        : panel(history ? layout(47002, 47040) : layout(47002, 47040, {47016}), disk_layout(history),
                layout(49002, 49060, {}, {49015, 49018}), layout(66001, 66060, {66028, 66038}), &texts) {
    }  // seven voice rows

    /// Whether gadget `id` of the page shown answers the mouse.
    bool answers(std::int32_t id, OptionsModel& model) {
        const auto at = panel.panel().position(id);
        REQUIRE(at.has_value());
        panel.mouse((*at)[0] + 5, (*at)[1] + 5, false, model);
        return panel.panel().hovered() == id;
    }

    /// A click on gadget `id` of the page shown.
    std::vector<OptionsEvent> click(std::int32_t id, OptionsModel& model) {
        const auto at = panel.panel().position(id);
        REQUIRE(at.has_value());
        const std::int32_t x = (*at)[0] + 5;
        const std::int32_t y = (*at)[1] + 5;
        std::vector<OptionsEvent> out = panel.mouse(x, y, true, model);
        for (OptionsEvent& e : panel.mouse(x, y, false, model)) {
            out.push_back(std::move(e));
        }
        return out;
    }
};

std::size_t count(const std::vector<OptionsEvent>& events, OptionsEvent::Kind kind) {
    return static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [&](const OptionsEvent& e) { return e.kind == kind; }));
}

}  // namespace

TEST_CASE("options panel: the first page switches full screen and the mission window and opens the pages") {
    Options o;
    OptionsModel model;
    o.panel.opened();
    o.panel.refresh(model);
    auto events = o.click(47022, model);  // full screen
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == OptionsEvent::Kind::Fullscreen);
    CHECK(events[0].value == 1);
    CHECK(model.fullscreen);
    events = o.click(47021, model);  // window
    REQUIRE(events.size() == 1);
    CHECK(!model.fullscreen);
    events = o.click(47015, model);
    CHECK(count(events, OptionsEvent::Kind::MissionWindow) == 1);
    CHECK(model.mission_window);
    CHECK(count(o.click(47023, model), OptionsEvent::Kind::Display) == 1);
    CHECK(!o.panel.back());  // nothing to go back from

    const ui::Panel* first = &o.panel.panel();
    CHECK(o.click(47012, model).empty());
    CHECK(&o.panel.panel() != first);
    CHECK(o.panel.back());
    CHECK(&o.panel.panel() == first);
}

TEST_CASE("options panel: music and effects change the audio settings") {
    Options o;
    OptionsModel model;
    model.tracks = {"One", "Two"};
    o.panel.opened();
    o.panel.refresh(model);
    o.click(47012, model);
    o.panel.refresh(model);
    const std::int32_t volume = model.audio.music_volume;
    CHECK(count(o.click(49014, model), OptionsEvent::Kind::Audio) == 1);
    CHECK(model.audio.music_volume == volume + 50);
    o.click(49016, model);
    CHECK(model.audio.effect_volume == -50);
    o.click(49021, model);
    CHECK(!model.audio.random_music);
    o.click(49024, model);
    CHECK(!model.audio.music);
    CHECK(count(o.click(49023, model), OptionsEvent::Kind::TestEffects) == 1);
    // A title's box switches it off; its name plays it (and switches it on again).
    o.click(49032, model);
    CHECK(model.audio.disabled_songs == std::vector<std::string>{"Two"});
    const auto events = o.click(49052, model);
    REQUIRE(count(events, OptionsEvent::Kind::PlayTrack) == 1);
    CHECK(std::find_if(events.begin(), events.end(), [](const OptionsEvent& e) {
              return e.kind == OptionsEvent::Kind::PlayTrack && e.text == "Two";
          }) != events.end());
    CHECK(model.audio.disabled_songs.empty());
}

TEST_CASE("options panel: the savegames are named, saved, loaded and the game left") {
    Options o;
    OptionsModel model;
    model.slot_names = {"First", ""};
    model.slot_humans = {1, 0};
    o.panel.opened();
    o.panel.refresh(model);
    o.click(47011, model);
    o.panel.refresh(model);

    // A free slot: its name is typed and Enter saves at once.
    CHECK(o.click(48062, model).empty());
    CHECK(o.panel.editing());
    o.panel.type("Newx");
    CHECK(!o.panel.key(8).has_value());  // Backspace
    const auto saved = o.panel.key(13);
    REQUIRE(saved.has_value());
    CHECK(saved->kind == OptionsEvent::Kind::Save);
    CHECK(saved->value == 1);
    CHECK(saved->text == "New");
    CHECK(!o.panel.editing());

    // A used slot: a new name waits for "Sichern"; Escape drops it.
    o.click(48061, model);
    o.panel.type(" game");
    CHECK(!o.panel.key(13).has_value());
    auto events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == OptionsEvent::Kind::Save);
    CHECK(events[0].value == 0);
    CHECK(events[0].text == "First game");
    o.click(48061, model);
    o.panel.type("lost");
    CHECK(!o.panel.key(27).has_value());
    events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].text == "First");

    events = o.click(48012, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == OptionsEvent::Kind::Load);
    CHECK(events[0].value == 0);
    CHECK(count(o.click(48013, model), OptionsEvent::Kind::Quit) == 1);
}

TEST_CASE("options panel: the History Edition's extra buttons are left out, its own arrows scroll") {
    Options o(true);
    OptionsModel model;
    for (int i = 0; i < 13; ++i) {
        model.slot_names.push_back("Game " + std::to_string(i));
    }
    o.panel.opened();
    o.panel.refresh(model);
    CHECK(!o.answers(47016, model));  // its settings screen: Open1602 has none
    CHECK(o.answers(47015, model));
    o.click(47011, model);
    o.panel.refresh(model);
    CHECK(!o.answers(48014, model));                         // a new savegame: the free slots do that
    CHECK(o.panel.panel().position(48081) == std::nullopt);  // no arrows of MUSIK.GAD
    CHECK(o.answers(48070, model));                          // ten rows, all with savegames
    CHECK(o.answers(48017, model));

    // Down by one: the last row shows the eleventh savegame.
    o.click(48016, model);
    o.panel.refresh(model);
    o.click(48070, model);
    CHECK(!o.panel.key(27).has_value());  // Escape: no new name
    auto events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == OptionsEvent::Kind::Save);
    CHECK(events[0].value == 10);
    CHECK(events[0].text == "Game 10");
    o.click(48017, model);
    o.panel.refresh(model);
    o.click(48061, model);
    CHECK(!o.panel.key(27).has_value());  // Escape: no new name
    events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].value == 0);
}

TEST_CASE("options panel: the mouse wheel scrolls the savegames and the music titles a row per notch") {
    Options o(true);
    OptionsModel model;
    for (int i = 0; i < 13; ++i) {
        model.slot_names.push_back("Game " + std::to_string(i));
    }
    model.tracks = {"1", "2", "3", "4", "5", "6", "7", "8"};
    o.panel.opened();
    o.panel.refresh(model);
    CHECK(!o.panel.wheel(-1, model));  // the first page has no list
    o.click(47011, model);
    o.panel.refresh(model);
    CHECK(o.panel.wheel(-2, model));  // down two rows
    o.panel.refresh(model);
    o.click(48061, model);
    CHECK(!o.panel.key(27).has_value());
    auto events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].value == 2);
    CHECK(o.panel.wheel(-10, model));  // not past the end: ten rows show savegames 3..12
    o.panel.refresh(model);
    o.click(48070, model);
    CHECK(!o.panel.key(27).has_value());
    events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].value == 12);
    CHECK(o.panel.wheel(5, model));  // up, not before the first
    o.panel.refresh(model);
    o.click(48061, model);
    CHECK(!o.panel.key(27).has_value());
    events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].value == 0);

    CHECK(o.panel.back());
    o.click(47012, model);
    o.panel.refresh(model);
    CHECK(o.panel.wheel(-1, model));
    o.panel.refresh(model);
    const auto played = o.click(49051, model);  // the first title shown is now the second
    REQUIRE(count(played, OptionsEvent::Kind::PlayTrack) == 1);
    CHECK(std::find_if(played.begin(), played.end(), [](const OptionsEvent& e) {
              return e.kind == OptionsEvent::Kind::PlayTrack && e.text == "2";
          }) != played.end());
}

TEST_CASE("options panel: the original's last savegame row gives its place to the arrows") {
    Options o;
    OptionsModel model;
    for (int i = 0; i < 13; ++i) {
        model.slot_names.push_back("Game " + std::to_string(i));
    }
    o.panel.opened();
    o.click(47011, model);
    o.panel.refresh(model);
    CHECK(o.answers(48071, model));
    CHECK(!o.answers(48072, model));  // under the arrows
    o.click(48081, model);            // down
    o.panel.refresh(model);
    o.click(48071, model);
    CHECK(!o.panel.key(27).has_value());  // Escape: no new name
    const auto events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].value == 11);
}

TEST_CASE("options panel: the last game in the first slot loads but is never named or saved into") {
    Options o;
    OptionsModel model;
    model.slot_names = {"lastgame", "Mine", ""};
    model.slot_humans = {1, 1, 0};
    model.reserved_slots = 1;
    o.panel.opened();
    o.panel.refresh(model);
    o.click(47011, model);
    o.panel.refresh(model);

    CHECK(o.click(48061, model).empty());
    CHECK(!o.panel.editing());
    o.panel.refresh(model);
    CHECK(o.click(48011, model).empty());  // "Sichern"
    auto events = o.click(48012, model);   // "Laden"
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == OptionsEvent::Kind::Load);
    CHECK(events[0].value == 0);

    // The slots after it as before.
    o.click(48062, model);
    CHECK(o.panel.editing());
    CHECK(!o.panel.key(27).has_value());
    o.panel.refresh(model);
    events = o.click(48011, model);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == OptionsEvent::Kind::Save);
    CHECK(events[0].value == 1);
}

TEST_CASE("options panel: voice messages and videos per kind") {
    Options o;
    OptionsModel model;
    o.panel.opened();
    o.panel.refresh(model);
    o.click(47013, model);
    o.panel.refresh(model);
    o.click(66022, model);  // the second kind
    CHECK(!model.audio.speech[2]);
    o.click(66004, model);  // all voice messages off: the kinds stay as they are
    CHECK(!model.audio.speech[0]);
    o.click(66023, model);  // and take no clicks
    CHECK(model.audio.speech[3]);
    CHECK(!model.audio.speech[2]);
    o.click(66041, model);  // kinds 1 and 2 share a box
    CHECK(!model.audio.video[1]);
    CHECK(!model.audio.video[2]);
}

TEST_CASE("options panel: the trade routes get an eighth voice row like the seven above it") {
    Options o;
    OptionsModel model;
    o.panel.opened();
    o.panel.refresh(model);
    o.click(47013, model);
    o.panel.refresh(model);
    const ui::Panel& page = o.panel.panel();
    const auto seventh = page.position(66027);
    const auto eighth = page.position(66028);
    REQUIRE(seventh.has_value());
    REQUIRE(eighth.has_value());
    CHECK((*eighth)[0] == (*seventh)[0]);
    CHECK((*eighth)[1] == (*seventh)[1] + 25);
    REQUIRE(page.position(66038).has_value());
    CHECK((*page.position(66038))[1] == (*page.position(66037))[1] + 25);
    // The video part's picture in three pieces: the voice box's edge under the eighth row.
    REQUIRE(page.position(66091).has_value());
    CHECK((*page.position(66091))[1] == (*page.position(66005))[1] + 13);
    o.click(66028, model);
    CHECK(!model.audio.speech[audio::kSpeechRoutes]);
    CHECK(model.audio.speech[7]);
    o.click(66028, model);
    CHECK(model.audio.speech[audio::kSpeechRoutes]);
}

TEST_CASE("options panel: a held volume slider is heard at once and saved once when let go") {
    Options o;
    OptionsModel model;
    o.panel.opened();
    o.panel.refresh(model);
    o.click(47012, model);
    o.panel.refresh(model);
    const auto at = o.panel.panel().position(49015);  // music
    REQUIRE(at.has_value());
    const auto live = [](const std::vector<OptionsEvent>& events) {
        return std::count_if(events.begin(), events.end(),
                             [](const OptionsEvent& e) { return e.kind == OptionsEvent::Kind::Audio && e.value == 1; });
    };
    const auto saved = [](const std::vector<OptionsEvent>& events) {
        return std::count_if(events.begin(), events.end(),
                             [](const OptionsEvent& e) { return e.kind == OptionsEvent::Kind::Audio && e.value == 0; });
    };
    const std::int32_t y = (*at)[1] + 5;
    auto events = o.panel.mouse((*at)[0] + 5, y, true, model);
    CHECK(live(events) == 1);
    CHECK(saved(events) == 0);
    const std::int32_t volume = model.audio.music_volume;
    CHECK(volume < 0);
    CHECK(volume > -4000);
    // Held still for a while: nothing new to hear, nothing written.
    for (int frame = 0; frame < 100; ++frame) {
        events = o.panel.mouse((*at)[0] + 5, y, true, model);
        CHECK(events.empty());
    }
    events = o.panel.mouse((*at)[0] + 15, y, true, model);
    CHECK(live(events) == 1);
    CHECK(saved(events) == 0);
    CHECK(model.audio.music_volume > volume);
    events = o.panel.mouse((*at)[0] + 15, y, false, model);
    CHECK(live(events) == 0);
    CHECK(saved(events) == 1);
    // A click elsewhere afterwards saves nothing more for the slider.
    events = o.panel.mouse(0, 0, false, model);
    CHECK(saved(events) == 0);
}
