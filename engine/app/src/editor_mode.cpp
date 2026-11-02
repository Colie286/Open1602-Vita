// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor as a mode of the game (ADR 0013): the viewer shows the document's world with the
// game's renderer and frame, the simulation never runs on it, and the side panel below the head is
// the editor's (editor_panel.hpp). The document is always saved as an Open1602 scenario.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>

#include "map_viewer.hpp"
#include "names.hpp"

#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/editor/islands.hpp"
#include "o1602/editor/original_check.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/font.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr std::string_view kScenarioEnding = ".o1602";
/// The folder of own islands (the editor's "Save island"), in the data folder.
constexpr std::string_view kOwnIslandsFolder = "islands";

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool own_file(const std::filesystem::path& path) {
    return lower(core::path_to_utf8(path.extension())) == kScenarioEnding;
}

/// The name a document shows for its file (Windows-1252): the file's name without its ending.
std::string shown_name(const std::filesystem::path& path) {
    return core::utf8_to_cp1252(core::path_to_utf8(path.stem()));
}

/// The files with one of `endings` (lower case) in `folder`, sorted by name.
std::vector<std::filesystem::path> files_in(const std::filesystem::path& folder,
                                            std::initializer_list<std::string_view> endings) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::string ending = lower(core::path_to_utf8(it->path().extension()));
        if (std::find(endings.begin(), endings.end(), ending) != endings.end()) {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return lower(core::path_to_utf8(a.filename())) < lower(core::path_to_utf8(b.filename()));
    });
    return out;
}

std::string number(std::size_t n) {
    return std::to_string(n);
}

std::string tiles(std::uint32_t x, std::uint32_t y) {
    return ui::tr("editor.prop.at", {number(x), number(y)});
}

/// The name of a building as the build menu calls it, else by its number. A field in another
/// stage of growth than the one the build menu offers is named as that one.
std::string building_name(const assets::BuildingType& b, const assets::BuildingTable& buildings,
                          const assets::TextTable* texts) {
    if (texts != nullptr) {
        for (const assets::BuildingType* t : {&b, sim::growing_record(buildings, b), sim::ripe_record(buildings, b)}) {
            const ui::BuildCategory* cat = t != nullptr ? ui::build_menu().category_of(t->id) : nullptr;
            if (cat == nullptr) {
                continue;
            }
            std::string name = cat->name(cat->index_of(t->id).value_or(0), texts->section(cat->names));
            if (!name.empty()) {
                return name;
            }
        }
    }
    return ui::tr("editor.prop.building_number", {number(b.id)});
}

/// The name of the player `owner` (Windows-1252), a dash for nobody.
std::string owner_name(const sim::World& world, sim::PlayerId owner) {
    const sim::Player* p = world.player(owner);
    return p != nullptr ? player_name(*p) : std::string("-");
}

/// The check list's line of a problem with the original's limits, in the player's language.
std::string limit_text(const editor::LimitProblem& p) {
    using Kind = editor::LimitProblem::Kind;
    const std::string island = number(p.island + 1);
    switch (p.kind) {
        case Kind::MapSize:
            return ui::tr("editor.limit.map_size", {number(p.width), number(p.height)});
        case Kind::Islands:
            return ui::tr("editor.limit.islands", {number(p.count), number(p.limit)});
        case Kind::Cities:
            return ui::tr("editor.limit.cities", {number(p.count), number(p.limit)});
        case Kind::Ships:
            return ui::tr("editor.limit.ships", {number(p.count), number(p.limit)});
        case Kind::Soldiers:
            return ui::tr("editor.limit.soldiers", {number(p.count), number(p.limit)});
        case Kind::Towers:
            return ui::tr("editor.limit.towers", {number(p.count), number(p.limit)});
        case Kind::Players:
            return ui::tr("editor.limit.players", {number(p.count), number(p.limit)});
        case Kind::IslandSize:
            return ui::tr("editor.limit.island_size", {island, number(p.width), number(p.height), number(p.limit)});
        case Kind::Deposits:
            return ui::tr("editor.limit.deposits", {island, number(p.count), number(p.limit)});
        case Kind::PackageContent:
            return ui::tr("editor.limit.package_content");
        case Kind::PackageClasses:
            return ui::tr("editor.limit.package_classes");
        case Kind::PackageFactions:
            return ui::tr("editor.limit.package_factions");
        case Kind::Balance:
            return ui::tr("editor.limit.balance");
        case Kind::Script:
            return ui::tr("editor.limit.script");
    }
    return {};
}

}  // namespace

sim::World MapViewer::sea_world(std::uint32_t width, std::uint32_t height) const {
    sim::World world(width, height);
    give_mod_content(world);
    return world;
}

core::Status MapViewer::enter_editor(sim::World world, const std::filesystem::path& file) {
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "no editor while a multiplayer game runs");
    }
    std::unique_ptr<GameState> next;
    try {
        next = make_state(std::move(world), m_options.rules, true);
    } catch (const std::bad_alloc&) {
        if (m_state) {
            // The old world's building values back, as replace_world does.
            sim::apply_building_values(m_buildings, m_original_buildings, m_state->world.balance());
            sim::apply_resident_classes(m_buildings, m_original_buildings, m_state->world.resident_classes());
            sim::apply_infra_levels(m_buildings, m_original_buildings, m_state->world.infra_levels());
            sim::apply_figure_values(m_figures, m_original_figures, m_state->world.balance());
        }
        return core::make_error(core::ErrorCode::PlatformError, "not enough memory for the map");
    }
    finish_autosave();  // the old game's, before its world goes
    // The old document edits the old world: it goes first.
    m_editor.reset();
    m_editor_pending.reset();
    adopt_state(std::move(next), file);
    // What the editor saves is a scenario: one with a description from the start, so that saving
    // a map that had none (a new one, a savegame) is not a step of its own to undo.
    if (!m_state->world.scenario_info()) {
        m_state->world.set_scenario_info(sim::ScenarioInfo{});
    }
    m_editor.emplace(EditorSession{editor::Document::edit(m_state->world),
                                   own_file(file) ? file : std::filesystem::path(),
                                   file.empty() ? std::string() : shown_name(file),
                                   {},
                                   false,
                                   {},
                                   {}});
    m_input->set_editor(true);
    if (m_hud) {
        m_hud->close_menu();
        m_hud->set_editing(true);
    }
    std::fprintf(stderr, "editor: %s, %ux%u, %zu islands\n",
                 file.empty() ? "new map" : core::path_to_utf8(file).c_str(), m_state->world.width(),
                 m_state->world.height(), m_state->world.islands().size());
    return core::Ok{};
}

void MapViewer::start_editor() {
    if (!m_options.editor_file.empty()) {
        if (auto opened = editor_open(m_options.editor_file); opened) {
            return;
        } else {
            std::fprintf(stderr, "error: --editor %s: %s\n", core::path_to_utf8(m_options.editor_file).c_str(),
                         opened.error().message.c_str());
        }
    }
    if (auto status = enter_editor(sea_world(sim::World::kOriginalWidth, sim::World::kOriginalHeight), {}); !status) {
        std::fprintf(stderr, "error: editor: %s\n", status.error().message.c_str());
    }
}

core::Status MapViewer::editor_open(const std::filesystem::path& file) {
    auto bytes = core::read_file(file);
    if (!bytes) {
        return bytes.error();
    }
    std::vector<std::string> warnings;
    auto world = editor::Document::read(*bytes, m_buildings, &m_game, &warnings, &m_figures);
    if (!world) {
        return world.error();
    }
    if (!sim::is_open1602_save(*bytes)) {
        give_mod_content(*world);  // the original's files have none of their own
    }
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    auto entered = enter_editor(std::move(world).value(), file);
    if (entered && !warnings.empty()) {
        editor_notice(ui::tr("notice.load_problems", {std::to_string(warnings.size())}), false,
                      std::to_string(warnings.size()) + " warnings while opening");
    }
    return entered;
}

core::Status MapViewer::editor_new(const EditorNewMap& map) {
    if (map.islands == EditorNewMap::Islands::None) {
        return enter_editor(sea_world(map.width(), map.height()), {});
    }
    auto texts = assets::TextTable::load(m_game);
    if (!texts) {
        return texts.error();
    }
    sim::NewGameOptions o;
    o.world = m_options.world;
    o.world.seed = map.seed;
    o.layout = sim::layout_for_seed(map.seed);
    o.name = m_app_settings.player_name.empty() ? ui::tr("menu.default_name") : m_app_settings.player_name;
    sim::WorldSettings custom;
    custom.width = map.width();
    custom.height = map.height();
    custom.players = std::clamp<std::uint8_t>(map.players, 1, static_cast<std::uint8_t>(sim::kMaxPlayers));
    custom.islands = map.islands == EditorNewMap::Islands::Few    ? sim::WorldSettings::Amount::Few
                     : map.islands == EditorNewMap::Islands::Many ? sim::WorldSettings::Amount::Many
                                                                  : sim::WorldSettings::Amount::Normal;
    custom.seed = map.seed;
    o.custom = sim::fit_landscape(custom);
    std::vector<std::string> warnings;
    auto world = sim::start_new_game(m_game, m_buildings, *texts, o, &warnings);
    if (!world) {
        return world.error();
    }
    give_mod_content(*world);
    name_more_players(*world);
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    return enter_editor(std::move(world).value(), {});
}

void MapViewer::editor_notice(std::string text, bool error, std::string_view log) {
    std::fprintf(stderr, "editor: %.*s\n", static_cast<int>(log.size()), log.data());
    if (m_editor) {
        m_editor->notice = std::move(text);
        m_editor->notice_error = error;
    }
}

void MapViewer::editor_save(const std::filesystem::path& file) {
    if (!m_editor) {
        return;
    }
    const auto target = prepare_save(file);
    if (!target) {
        editor_notice(ui::tr("editor.save_failed", {ui::error_text(target.error(), "editor")}), true,
                      target.error().message);
        return;
    }
    auto bytes = m_editor->document.save(m_buildings);
    if (!bytes) {
        editor_notice(ui::tr("editor.save_failed", {ui::error_text(bytes.error(), "editor")}), true,
                      bytes.error().message);
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(target->parent_path(), ec);
    if (auto written = core::write_file_atomic(*target, *bytes); !written) {
        editor_notice(ui::tr("editor.save_failed", {ui::error_text(written.error(), "editor")}), true,
                      written.error().message);
        return;
    }
    m_editor->file = *target;
    m_editor->source_name = shown_name(*target);
    editor_notice(ui::tr("editor.saved", {shown_name(*target)}), false, "saved " + core::path_to_utf8(*target));
}

void MapViewer::leave_editor() {
    m_editor.reset();
    m_editor_pending.reset();
    m_input->set_editor(false);
    if (m_hud) {
        m_hud->set_editing(false);
        m_hud->open_menu();
    } else {
        m_running = false;
    }
    std::fprintf(stderr, "editor: left\n");
}

void MapViewer::list_editor_files() {
    m_editor_files.clear();
    m_editor_file_rows.clear();
    const std::string own = ui::tr("editor.open.own");
    for (const std::filesystem::path& p : files_in(own_scenario_folder(), {kScenarioEnding})) {
        m_editor_files.push_back(p);
        m_editor_file_rows.push_back(EditorFile{shown_name(p), own});
    }
    if (const auto folder = m_game.resolve("Szenes")) {
        const std::string original = ui::tr("editor.open.original");
        for (const std::filesystem::path& p : files_in(*folder, {".szs", ".szm"})) {
            m_editor_files.push_back(p);
            m_editor_file_rows.push_back(EditorFile{core::utf8_to_cp1252(core::path_to_utf8(p.filename())), original});
        }
    }
}

EditorModel MapViewer::editor_model() const {
    EditorModel m;
    if (!m_editor) {
        return m;
    }
    const editor::Document& doc = m_editor->document;
    const sim::World& world = doc.world();
    m.file = m_editor->file.empty() ? m_editor->source_name : shown_name(m_editor->file);
    m.modified = doc.modified();
    m.width = world.width();
    m.height = world.height();
    m.islands = world.islands().size();
    if (const auto& planned = world.planned_islands()) {
        m.planned_islands = planned->plan.islands.size();
    }
    m.players =
        static_cast<std::size_t>(std::count_if(world.players().begin(), world.players().end(), [](const auto& p) {
            return p.role == sim::PlayerRole::Human || p.role == sim::PlayerRole::Computer;
        }));
    m.can_undo = doc.history().can_undo();
    m.can_redo = doc.history().can_redo();
    m.notice = m_editor->notice;
    m.notice_is_error = m_editor->notice_error;
    m.files = m_editor_file_rows;
    editor_properties(m);
    m.overlays = {m_show_owners, m_show_island_numbers, m_show_coordinates};
    m.check = m_editor->check;
    if (m_editor->pick.kind == EditorPick::Kind::Island && m_editor->pick.island < world.islands().size()) {
        m.island = m_editor->pick.island;
        m.island_free = world.island_use(m_editor->pick.island) == sim::World::IslandUse::None;
        m.island_fertility = world.islands()[m_editor->pick.island].header.fertility;
        m.island_climate = world.islands()[m_editor->pick.island].header.climate;
    }
    m.placing = m_editor->placing.has_value();
    if (m_hud && m_hud->editor_panel() && m_hud->editor_panel()->page() == EditorPanel::Page::WorldSize) {
        const auto [w, h] = editor::used_size(world);
        m.used_width = w;
        m.used_height = h;
    }
    if (m_hud && m_hud->editor_panel() && m_hud->editor_panel()->page() == EditorPanel::Page::SaveAs) {
        // Only while a name is typed: the folder is read every frame then.
        for (const std::filesystem::path& p : files_in(data_folder() / kOwnScenariosFolder, {kScenarioEnding})) {
            m.own_names.push_back(shown_name(p));
        }
    }
    return m;
}

void MapViewer::editor_request(const EditorRequest& request, bool confirmed) {
    if (!m_editor) {
        return;
    }
    using Kind = EditorRequest::Kind;
    const bool loses = request.kind == Kind::New || request.kind == Kind::Open || request.kind == Kind::Close;
    if (loses && !confirmed && m_editor->document.modified() && m_hud) {
        m_editor_pending = request;
        m_hud->ask(ui::tr("editor.discard"));
        return;
    }
    switch (request.kind) {
        case Kind::New: {
            m_editor_new = request.map;
            if (auto made = editor_new(request.map); !made) {
                editor_notice(ui::tr("editor.new_failed", {ui::error_text(made.error(), "editor")}), true,
                              made.error().message);
            }
            break;
        }
        case Kind::Open:
            if (request.index < m_editor_files.size()) {
                const std::filesystem::path file = m_editor_files[request.index];
                if (auto opened = editor_open(file); !opened) {
                    editor_notice(ui::tr("editor.open_failed", {ui::error_text(opened.error(), "editor")}), true,
                                  opened.error().message);
                }
            }
            break;
        case Kind::Save:
            if (m_editor->file.empty()) {
                if (EditorPanel* panel = m_hud ? m_hud->editor_panel() : nullptr) {
                    panel->ask_name(m_editor->source_name);
                }
            } else {
                editor_save(m_editor->file);
            }
            break;
        case Kind::SaveAs: {
            const std::string name = EditorPanel::clean_name(request.name);
            if (!name.empty()) {
                editor_save(own_scenario_folder() /
                            core::path_from_utf8(core::cp1252_to_utf8(name) + std::string(kScenarioEnding)));
            }
            break;
        }
        case Kind::Undo: {
            const std::string what = ui::tr("editor.step." + std::string(m_editor->document.history().next_undo()));
            if (m_editor->document.undo()) {
                editor_islands_changed();
                editor_notice(ui::tr("editor.undone", {what}), false, "undo");
            }
            break;
        }
        case Kind::Redo: {
            const std::string what = ui::tr("editor.step." + std::string(m_editor->document.history().next_redo()));
            if (m_editor->document.redo()) {
                editor_islands_changed();
                editor_notice(ui::tr("editor.redone", {what}), false, "redo");
            }
            break;
        }
        case Kind::Close:
            leave_editor();
            break;
        case Kind::ListFiles:
            list_editor_files();
            break;
        case Kind::Overlay:
            switch (static_cast<EditorOverlay>(request.index)) {
                case EditorOverlay::Owners:
                    m_show_owners = !m_show_owners;
                    break;
                case EditorOverlay::IslandNumbers:
                    m_show_island_numbers = !m_show_island_numbers;
                    break;
                case EditorOverlay::Coordinates:
                    m_show_coordinates = !m_show_coordinates;
                    break;
            }
            break;
        case Kind::Check:
            m_editor->check = editor_check();
            break;
        case Kind::PlaceIsland:
        case Kind::MoveIsland:
        case Kind::RotateIsland:
        case Kind::CopyIsland:
        case Kind::RemoveIsland:
        case Kind::StopPlacing:
            editor_island_request(request);
            break;
        case Kind::SaveIsland:
            editor_save_island(request.index);
            break;
        case Kind::ListIslandFiles:
            m_editor_files.clear();
            m_editor_file_rows.clear();
            for (const std::filesystem::path& p : files_in(own_island_folder(), {kScenarioEnding, ".scp"})) {
                m_editor_files.push_back(p);
                m_editor_file_rows.push_back(EditorFile{shown_name(p), ui::tr("editor.islands.own_files")});
            }
            break;
        case Kind::LoadIsland:
            if (request.index < m_editor_files.size()) {
                editor_load_island(m_editor_files[request.index]);
            }
            break;
        case Kind::SetFertility: {
            auto op = editor::set_fertility(m_editor->document.world(), request.index, request.fertility);
            if (op) {
                m_editor->document.perform(std::move(op).value());
            }
            break;
        }
        case Kind::ResizeWorld: {
            const sim::World& world = m_editor->document.world();
            if (!world.simulation_state().empty()) {
                editor_notice(ui::tr("editor.islands.in_use.game"), true, "the map of a running game");
                break;
            }
            auto op = editor::resize_world(world, request.width, request.height, m_buildings);
            if (!op) {
                editor_notice(ui::tr("editor.world_size.failed"), true, op.error().message);
                break;
            }
            m_editor->document.perform(std::move(op).value());
            editor_islands_changed();
            editor_notice(ui::tr("editor.world_size.done", {number(request.width), number(request.height)}), false,
                          "map size changed");
            break;
        }
    }
}

namespace {

/// The note of why an island cannot be turned, copied or taken out; empty if it can.
std::string use_note(sim::World::IslandUse use) {
    using Use = sim::World::IslandUse;
    switch (use) {
        case Use::None:
            return {};
        case Use::Cities:
            return ui::tr("editor.islands.in_use.cities");
        case Use::Buildings:
            return ui::tr("editor.islands.in_use.buildings");
        case Use::Units:
            return ui::tr("editor.islands.in_use.units");
        case Use::RunningGame:
            return ui::tr("editor.islands.in_use.game");
    }
    return {};
}

}  // namespace

void MapViewer::editor_island_request(const EditorRequest& request) {
    using Kind = EditorRequest::Kind;
    using Placing = EditorSession::Placing;
    const sim::World& world = m_editor->document.world();
    if (request.kind == Kind::StopPlacing) {
        (void)editor_cancel_place();
        return;
    }
    if (request.kind == Kind::PlaceIsland) {
        const auto number = world.free_island_number();
        editor::NewIsland wanted;
        wanted.shape = request.island.own_shape ? editor::NewIsland::Shape::Own : editor::NewIsland::Shape::Template;
        wanted.climate = request.island.climate;
        wanted.size = request.island.size;
        wanted.seed = request.island.seed;
        std::vector<std::string> warnings;
        auto made = number ? editor::make_island(wanted, *number, m_buildings, &m_game, &warnings)
                           : core::Result<sim::WorldIsland>(
                                 core::make_error(core::ErrorCode::LimitExceeded, "no island number is free"));
        for (const std::string& w : warnings) {
            std::fprintf(stderr, "warning: %s\n", w.c_str());
        }
        if (!made) {
            editor_notice(ui::tr("editor.islands.failed", {ui::error_text(made.error(), "editor")}), true,
                          made.error().message);
            return;
        }
        Placing p;
        p.kind = Placing::Kind::New;
        p.width = made->map.width();
        p.height = made->map.height();
        p.island = std::move(made).value();
        p.page = EditorPanel::Page::Islands;
        m_editor->placing = std::move(p);
        editor_notice(ui::tr("editor.islands.placing"), false, "placing a new island");
        return;
    }
    if (request.index >= world.islands().size()) {
        return;
    }
    const std::string name = number(request.index + 1);
    const sim::World::IslandUse use = world.island_use(request.index);
    const bool moving = request.kind == Kind::MoveIsland;
    if (use != sim::World::IslandUse::None &&
        !(moving && use != sim::World::IslandUse::Units && use != sim::World::IslandUse::RunningGame)) {
        editor_notice(use_note(use), true, "the island is in use");
        return;
    }
    switch (request.kind) {
        case Kind::MoveIsland:
        case Kind::CopyIsland: {
            const sim::WorldIsland& island = world.islands()[request.index];
            Placing p;
            p.kind = moving ? Placing::Kind::Move : Placing::Kind::Copy;
            p.index = request.index;
            p.width = island.map.width();
            p.height = island.map.height();
            p.page = EditorPanel::Page::Selection;
            m_editor->placing = std::move(p);
            editor_notice(ui::tr("editor.islands.placing"), false, "placing an island");
            return;
        }
        case Kind::RotateIsland:
            if (editor_perform(editor::rotate_island(world, request.index, m_buildings))) {
                editor_notice(ui::tr("editor.islands.rotated", {name}), false, "island turned");
            }
            return;
        case Kind::RemoveIsland:
            if (editor_perform(editor::remove_island(world, request.index, m_buildings))) {
                m_editor->pick = EditorPick{};
                if (m_hud && m_hud->editor_panel() != nullptr) {
                    m_hud->editor_panel()->show_document();
                }
                editor_notice(ui::tr("editor.islands.removed", {name}), false, "island deleted");
            }
            return;
        default:
            return;
    }
}

std::filesystem::path MapViewer::own_island_folder() {
    const std::filesystem::path folder = data_folder() / kOwnIslandsFolder;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    return folder;
}

void MapViewer::editor_save_island(std::size_t index) {
    const sim::World& world = m_editor->document.world();
    if (index >= world.islands().size()) {
        return;
    }
    if (const sim::World::IslandUse use = world.island_use(index); use != sim::World::IslandUse::None) {
        editor_notice(use_note(use), true, "the island is in use");
        return;
    }
    auto bytes = editor::save_island(world, index, m_buildings);
    if (!bytes) {
        editor_notice(ui::tr("editor.save_failed", {ui::error_text(bytes.error(), "editor")}), true,
                      bytes.error().message);
        return;
    }
    // The first free name of "island 1", "island 2", ...
    const std::filesystem::path folder = own_island_folder();
    std::filesystem::path file;
    for (std::size_t n = 1;; ++n) {
        file = folder / (ui::tr("editor.islands.file_name", {number(n)}) + std::string(kScenarioEnding));
        std::error_code ec;
        if (!std::filesystem::exists(file, ec) || n > 9999) {
            break;
        }
    }
    if (auto written = core::write_file_atomic(file, *bytes); !written) {
        editor_notice(ui::tr("editor.save_failed", {ui::error_text(written.error(), "editor")}), true,
                      written.error().message);
        return;
    }
    editor_notice(ui::tr("editor.islands.saved", {shown_name(file)}), false,
                  "island saved " + core::path_to_utf8(file));
}

void MapViewer::editor_load_island(const std::filesystem::path& file) {
    auto bytes = core::read_file(file);
    std::vector<std::string> warnings;
    auto island = bytes ? editor::load_island(*bytes, m_buildings, &m_game, &warnings)
                        : core::Result<sim::WorldIsland>(bytes.error());
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    if (!island) {
        editor_notice(ui::tr("editor.islands.failed", {ui::error_text(island.error(), "editor")}), true,
                      island.error().message);
        return;
    }
    EditorSession::Placing p;
    p.kind = EditorSession::Placing::Kind::New;
    p.width = island->map.width();
    p.height = island->map.height();
    p.island = std::move(island).value();
    p.page = EditorPanel::Page::Islands;
    m_editor->placing = std::move(p);
    editor_notice(ui::tr("editor.islands.placing"), false, "placing an island from " + core::path_to_utf8(file));
}

bool MapViewer::editor_perform(core::Result<std::unique_ptr<editor::Operation>> op) {
    if (!op) {
        editor_notice(ui::tr("editor.islands.no_room"), true, op.error().message);
        return false;
    }
    m_editor->document.perform(std::move(op).value());
    editor_islands_changed();
    return true;
}

void MapViewer::editor_islands_changed() {
    if (!m_editor) {
        return;
    }
    m_building_figures = render::collect_building_figures(m_state->world, m_buildings, m_figures);
    m_camera.set_world_size(m_state->world.width(), m_state->world.height());
    if (m_hud) {
        m_hud->rebuild_minimap();
    }
    const std::size_t islands = m_state->world.islands().size();
    EditorPick& pick = m_editor->pick;
    if ((pick.kind == EditorPick::Kind::Island || pick.kind == EditorPick::Kind::Building) && pick.island >= islands) {
        pick = EditorPick{};
    }
    if (m_editor->placing && m_editor->placing->kind != EditorSession::Placing::Kind::New &&
        m_editor->placing->index >= islands) {
        m_editor->placing.reset();
    }
}

std::pair<std::int64_t, std::int64_t> MapViewer::editor_place_origin(const render::TilePos& tile) const {
    const auto& p = *m_editor->placing;
    return {std::int64_t{tile.x} - p.width / 2, std::int64_t{tile.y} - p.height / 2};
}

bool MapViewer::editor_place(const render::TilePos& tile) {
    if (!m_editor || !m_editor->placing) {
        return false;
    }
    using Kind = EditorSession::Placing::Kind;
    const EditorSession::Placing& p = *m_editor->placing;
    const sim::World& world = m_editor->document.world();
    const auto [x, y] = editor_place_origin(tile);
    const std::optional<std::size_t> except = p.kind == Kind::Move ? std::optional<std::size_t>(p.index) : std::nullopt;
    if (!editor::free_place(world, x, y, p.width, p.height, except)) {
        editor_notice(ui::tr("editor.islands.no_room"), true, "no room for the island");
        return true;
    }
    if (editor::ship_in_way(world, x, y, p.width, p.height, except)) {
        editor_notice(ui::tr("editor.islands.ship"), true, "a ship is in the way");
        return true;
    }
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    const std::size_t index = p.kind == Kind::Move ? p.index : world.islands().size();
    const bool moved = p.kind == Kind::Move;
    bool done = false;
    switch (p.kind) {
        case Kind::New:
            done = editor_perform(editor::add_island(world, *p.island, ux, uy, m_buildings));
            break;
        case Kind::Move:
            done = editor_perform(editor::move_island(world, p.index, ux, uy, m_buildings));
            break;
        case Kind::Copy:
            done = editor_perform(editor::copy_island(world, p.index, ux, uy, m_buildings));
            break;
    }
    if (!done) {
        return true;  // still in hand: another place may do
    }
    m_editor->placing.reset();
    // The island put down is the one the panel shows.
    EditorPick pick;
    pick.kind = EditorPick::Kind::Island;
    pick.island = static_cast<std::uint16_t>(index);
    pick.x = tile.x;
    pick.y = tile.y;
    if (m_hud && m_hud->editor_panel() != nullptr && m_hud->editor_panel()->show_selection()) {
        m_editor->pick = pick;
    }
    editor_notice(ui::tr(moved ? "editor.islands.moved" : "editor.islands.placed", {number(index + 1)}), false,
                  moved ? "island moved" : "island placed");
    return true;
}

bool MapViewer::editor_cancel_place() {
    if (!m_editor || !m_editor->placing) {
        return false;
    }
    m_editor->placing.reset();
    m_editor->notice.clear();
    return true;
}

void MapViewer::editor_pick(const EditorPick& pick) {
    if (!m_editor || !m_hud || m_hud->editor_panel() == nullptr) {
        return;
    }
    if (m_hud->editor_panel()->show_selection()) {
        m_editor->pick = pick;
    }
}

void MapViewer::editor_properties(EditorModel& m) const {
    const sim::World& world = m_editor->document.world();
    const EditorPick& pick = m_editor->pick;
    const auto add = [&](const char* label, std::string value) {
        m.properties.push_back(EditorProperty{ui::tr(label), std::move(value)});
    };
    using Kind = EditorPick::Kind;
    switch (pick.kind) {
        case Kind::None:
            return;
        case Kind::Sea:
            if (pick.x >= world.width() || pick.y >= world.height()) {
                return;
            }
            m.selection = ui::tr("editor.prop.sea");
            add("editor.prop.position", tiles(pick.x, pick.y));
            return;
        case Kind::Island:
        case Kind::Building: {
            if (pick.island >= world.islands().size()) {
                return;
            }
            const sim::WorldIsland& island = world.islands()[pick.island];
            if (pick.kind == Kind::Island) {
                m.selection = ui::tr("editor.prop.island", {number(pick.island + 1U)});
                add("editor.prop.position", tiles(island.x, island.y));
                add("editor.prop.size",
                    ui::tr("editor.size", {number(island.map.width()), number(island.map.height())}));
                add("editor.prop.climate",
                    ui::tr(island.header.climate % 2 == 0 ? "editor.prop.north" : "editor.prop.south"));
                add("editor.prop.deposits", number(island.header.deposits.size()));
                std::size_t cities = 0;
                for (const sim::City& c : world.cities()) {
                    cities += c.island == pick.island ? 1U : 0U;
                }
                add("editor.prop.cities", number(cities));
                return;
            }
            if (pick.x >= island.map.width() || pick.y >= island.map.height()) {
                return;
            }
            const sim::SubTile t = island.map.sub_tile(m_buildings, pick.x, pick.y);
            if (t.building == nullptr || t.origin_x != static_cast<std::int32_t>(pick.x) ||
                t.origin_y != static_cast<std::int32_t>(pick.y)) {
                return;  // gone
            }
            m.selection = building_name(*t.building, m_buildings, m_hud ? m_hud->texts() : nullptr);
            add("editor.prop.island_number", number(pick.island + 1U));
            add("editor.prop.position", tiles(island.x + pick.x, island.y + pick.y));
            const std::uint8_t city = island.map.top(pick.x, pick.y).city;
            for (const sim::City& c : world.cities()) {
                if (c.island == pick.island && c.index == city) {
                    add("editor.prop.city", city_name(c));
                    add("editor.prop.owner", owner_name(world, c.owner));
                    break;
                }
            }
            return;
        }
        case Kind::Ship: {
            const sim::Ship* ship = world.ship(pick.id);
            if (ship == nullptr) {
                return;
            }
            m.selection = ship_name(*ship);
            add("editor.prop.owner", owner_name(world, ship->owner));
            add("editor.prop.position", tiles(ship->x, ship->y));
            add("editor.prop.cannons", number(ship->cannons));
            std::uint64_t load = 0;
            for (const sim::CargoSlot& slot : ship->cargo) {
                load += slot.ware != 0 ? slot.amount : 0U;
            }
            add("editor.prop.cargo", ui::tr("editor.prop.tons", {number(static_cast<std::size_t>(load / 32U))}));
            return;
        }
        case Kind::Soldier: {
            const sim::Soldier* soldier = world.soldier(pick.id);
            if (soldier == nullptr) {
                return;
            }
            m.selection = ui::tr("editor.prop.unit");
            add("editor.prop.owner", owner_name(world, soldier->owner));
            add("editor.prop.position", tiles(static_cast<std::uint32_t>(std::max(0, soldier->pos_x / 4096)),
                                              static_cast<std::uint32_t>(std::max(0, soldier->pos_y / 4096))));
            return;
        }
    }
}

std::vector<EditorCheckLine> MapViewer::editor_check() const {
    using Line = EditorCheckLine;
    std::vector<Line> lines;
    const editor::Document& doc = m_editor->document;
    const sim::World& world = doc.world();
    // What keeps the scenario from being saved: the world must hold together, its own parts be right.
    lines.push_back(Line{ui::tr("editor.check.saving"), Line::Kind::Heading});
    const std::size_t first = lines.size();
    if (auto valid = sim::validate(world); !valid) {
        lines.push_back(
            Line{ui::tr("editor.check.world", {core::utf8_to_cp1252(valid.error().message)}), Line::Kind::Error});
    }
    for (const std::string& p : doc.check_scenario()) {
        lines.push_back(Line{ui::tr("editor.check.scenario", {core::utf8_to_cp1252(p)}), Line::Kind::Error});
    }
    if (lines.size() == first) {
        lines.push_back(Line{ui::tr("editor.check.fine"), Line::Kind::Fine});
    }
    // What the original cannot have: a fault only while the scenario is to keep to its limits.
    const auto& info = world.scenario_info();
    const bool keep = info && info->original_limits;
    lines.push_back(Line{ui::tr(keep ? "editor.check.limits_kept" : "editor.check.limits"), Line::Kind::Heading});
    const auto problems = editor::original_limit_problems(world);
    for (const editor::LimitProblem& p : problems) {
        lines.push_back(Line{limit_text(p), keep ? Line::Kind::Warning : Line::Kind::Note});
    }
    if (problems.empty()) {
        lines.push_back(Line{ui::tr("editor.check.fits"), Line::Kind::Fine});
    }
    return lines;
}

void MapViewer::draw_editor_marks() {
    if (!m_editor || m_hud == nullptr) {
        return;
    }
    const sim::World& world = m_state->world;
    const render::WorldView view = render::fit_view(m_camera.view(), world);
    const ui::Rect& v = m_hud->game_view();
    std::vector<ui::DrawItem> items;
    const auto text_at = [&](double x, double y, std::string text) {
        const auto [px, py] = render::text_figure_point(view, x, y, 0.0);
        double sx = 0.0;
        double sy = 0.0;
        m_camera.world_to_screen(px, py, sx, sy);
        const auto ix = static_cast<std::int32_t>(std::floor(sx));
        const auto iy = static_cast<std::int32_t>(std::floor(sy));
        if (ix < v.x || ix > v.x + v.w || iy < v.y || iy > v.y + v.h) {
            return;
        }
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = 0;  // the normal font: larger than the small one of the corner's lines
        d.colour = 0;
        d.align = ui::Align::Centre;
        d.x = ix;
        d.y = iy;
        d.text = std::move(text);
        items.push_back(std::move(d));
    };
    if (m_show_island_numbers) {
        for (std::size_t i = 0; i < world.islands().size(); ++i) {
            const sim::WorldIsland& island = world.islands()[i];
            text_at(island.x + island.map.width() / 2.0, island.y + island.map.height() / 2.0,
                    ui::tr("editor.prop.island", {number(i + 1)}));
        }
    }
    // The island being placed: its outline under the mouse, green where it fits, red where not.
    if (m_editor->placing) {
        if (const auto tile = m_input->tile_at_mouse()) {
            const EditorSession::Placing& p = *m_editor->placing;
            const auto [x0, y0] = editor_place_origin(*tile);
            const std::optional<std::size_t> except =
                p.kind == EditorSession::Placing::Kind::Move ? std::optional<std::size_t>(p.index) : std::nullopt;
            const bool fits = editor::free_place(world, x0, y0, p.width, p.height, except) &&
                              !editor::ship_in_way(world, x0, y0, p.width, p.height, except);
            const std::int32_t ink =
                ui::rgb_ink(fits ? assets::Rgba{90, 230, 90, 255} : assets::Rgba{235, 70, 60, 255});
            const auto screen = [&](double x, double y) {
                const auto [px, py] = render::text_figure_point(view, x, y, 0.0);
                double sx = 0.0;
                double sy = 0.0;
                m_camera.world_to_screen(px, py, sx, sy);
                return std::pair<double, double>{sx, sy};
            };
            const double w = p.width;
            const double h = p.height;
            const std::array<std::pair<double, double>, 4> corners = {
                screen(static_cast<double>(x0), static_cast<double>(y0)),
                screen(static_cast<double>(x0) + w, static_cast<double>(y0)),
                screen(static_cast<double>(x0) + w, static_cast<double>(y0) + h),
                screen(static_cast<double>(x0), static_cast<double>(y0) + h)};
            // Each side as a dotted line: a dot of 3 x 3 pixels every 6.
            for (std::size_t i = 0; i < corners.size(); ++i) {
                const auto [ax, ay] = corners[i];
                const auto [bx, by] = corners[(i + 1) % corners.size()];
                const double length = std::hypot(bx - ax, by - ay);
                const auto dots = static_cast<std::int32_t>(std::min(4000.0, length / 6.0));
                for (std::int32_t d = 0; d <= dots; ++d) {
                    const double t = dots == 0 ? 0.0 : static_cast<double>(d) / dots;
                    const auto dx = static_cast<std::int32_t>(std::floor(ax + (bx - ax) * t)) - 1;
                    const auto dy = static_cast<std::int32_t>(std::floor(ay + (by - ay) * t)) - 1;
                    if (dx < v.x || dy < v.y || dx + 3 > v.x + v.w || dy + 3 > v.y + v.h) {
                        continue;
                    }
                    ui::DrawItem dot;
                    dot.kind = ui::DrawItem::Kind::Fill;
                    dot.x = dx;
                    dot.y = dy;
                    dot.w = 3;
                    dot.h = 3;
                    dot.colour = ink;
                    items.push_back(std::move(dot));
                }
            }
        }
    }
    // The mark of what the panel shows: a cross on the tile, or over the ship, unit or building.
    const EditorPick& pick = m_editor->pick;
    const bool shown =
        m_hud->editor_panel() != nullptr && m_hud->editor_panel()->page() == EditorPanel::Page::Selection;
    if (shown) {
        using Kind = EditorPick::Kind;
        const std::string mark = "\xD7";
        switch (pick.kind) {
            case Kind::None:
                break;
            case Kind::Sea:
            case Kind::Island:
                text_at(pick.x + 0.5, pick.y + 0.5, mark);
                break;
            case Kind::Building:
                if (pick.island < world.islands().size()) {
                    const sim::WorldIsland& island = world.islands()[pick.island];
                    text_at(island.x + pick.x + 0.5, island.y + pick.y + 0.5, mark);
                }
                break;
            case Kind::Ship:
                if (const sim::Ship* ship = world.ship(pick.id)) {
                    text_at(ship->pos_x / 4096.0, ship->pos_y / 4096.0, mark);
                }
                break;
            case Kind::Soldier:
                if (const sim::Soldier* soldier = world.soldier(pick.id)) {
                    text_at(soldier->pos_x / 4096.0, soldier->pos_y / 4096.0, mark);
                }
                break;
        }
    }
    if (!items.empty()) {
        m_hud->draw_over_map(items);
    }
}

void MapViewer::apply_editor() {
    if (!m_editor || !m_hud) {
        return;
    }
    for (const EditorRequest& r : m_hud->take_editor_requests()) {
        editor_request(r);
        if (!m_editor) {
            return;  // left the editor: the rest went with it
        }
    }
    // The page of the selection left (Escape, Back, another page): nothing is picked any more.
    if (m_hud->editor_panel() != nullptr && m_hud->editor_panel()->page() != EditorPanel::Page::Selection) {
        m_editor->pick = EditorPick{};
    }
    // The page an island is placed from left: it goes back.
    if (m_editor->placing &&
        (m_hud->editor_panel() == nullptr || m_hud->editor_panel()->page() != m_editor->placing->page)) {
        m_editor->placing.reset();
    }
    m_hud->set_editor_model(editor_model());
}

void MapViewer::editor_key(ui::KeyAction action) {
    switch (action) {
        case ui::KeyAction::EditorUndo:
            editor_request(EditorRequest{EditorRequest::Kind::Undo});
            break;
        case ui::KeyAction::EditorRedo:
            editor_request(EditorRequest{EditorRequest::Kind::Redo});
            break;
        case ui::KeyAction::EditorSave:
            editor_request(EditorRequest{EditorRequest::Kind::Save});
            break;
        default:
            break;
    }
}

}  // namespace o1602::app
