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
#include <tuple>

#include "endless_setup.hpp"
#include "map_viewer.hpp"
#include "names.hpp"

#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/editor/islands.hpp"
#include "o1602/editor/original_check.hpp"
#include "o1602/editor/resources.hpp"
#include "o1602/editor/units.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/island_shapes.hpp"
#include "o1602/sim/island_templates.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/font.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// The original's climate and the package's (WorldIsland::climate, 0 none) of the choice `choice`
/// of EditorNewIsland::climate: 0 north, 1 south, from 2 the world's package climates.
std::pair<std::uint8_t, std::uint8_t> new_island_climate(const sim::World& world, std::uint8_t choice) {
    const auto& climates = world.balance().climates;
    if (choice >= 2 && choice - 2U < climates.size()) {
        return {static_cast<std::uint8_t>(climates[choice - 2U].like & 1U), static_cast<std::uint8_t>(choice - 1U)};
    }
    return {static_cast<std::uint8_t>(choice & 1U), std::uint8_t{0}};
}

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
    apply_landscape_choice(custom, map.landscape);
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
        m.island_crops = sim::climate_crops(world.balance().climates, world.islands()[m_editor->pick.island]);
    }
    m.placing = m_editor->placing.has_value();
    if (m_hud && m_hud->editor_panel() && m_hud->editor_panel()->page() == EditorPanel::Page::New) {
        for (std::size_t i = 0; i < landscape_choices(); ++i) {
            m.landscapes.push_back(landscape_choice_name(i));
        }
    }
    for (const sim::Climate& c : world.balance().climates) {
        m.climates.push_back(c.name(ui::current_language()));
    }
    if (m_hud && m_hud->editor_panel() && m_hud->editor_panel()->page() == EditorPanel::Page::Islands) {
        const EditorNewIsland& wanted = m_hud->editor_panel()->new_island();
        const std::uint8_t climate = new_island_climate(world, wanted.climate).first;
        const std::uint8_t size = std::min<std::uint8_t>(wanted.size, sim::templates::kSizeClasses - 1);
        const auto& packaged = m_options.world.package_islands;
        std::uint16_t count = sim::templates::kTemplateCount[climate][size];
        for (const sim::PackageIsland& p : packaged) {
            count = static_cast<std::uint16_t>(count + (p.climate == climate && p.size == size ? 1 : 0));
        }
        const auto chosen = static_cast<std::uint16_t>(wanted.seed % std::max<std::uint16_t>(count, 1));
        const sim::PackageIsland* p = sim::package_template(packaged, climate, size, chosen);
        m.island_template = p != nullptr ? ui::tr("editor.islands.template_package",
                                                  {p->name(ui::current_language()), number(chosen + 1U), number(count)})
                                         : ui::tr("editor.islands.template", {number(chosen + 1U), number(count)});
    }
    m.has_plan = world.planned_islands().has_value();
    if (m_editor->pick.kind == EditorPick::Kind::Placeholder &&
        editor::placeholder_open(world, m_editor->pick.island)) {
        const assets::ScenarioPlan::Island& e = world.planned_islands()->plan.islands[m_editor->pick.island];
        m.placeholder = m_editor->pick.island;
        m.placeholder_climate = e.climate;
        m.placeholder_size = e.size;
    }
    m.terrain = m_editor->terrain;
    {
        const auto pieces =
            editor::terrain_pieces(m_buildings, static_cast<editor::TerrainGroup>(m_editor->terrain.group));
        m.terrain_pieces = pieces.size();
        if (const assets::BuildingType* b = editor_terrain_piece()) {
            m.terrain_id = b->id;
            m.terrain_width = b->size_x;
            m.terrain_height = b->size_y;
            m.terrain_turns = b->rotate > 0;
        }
    }
    editor_object_model(m);
    editor_city_model(m);
    m.terrain_selected = m_editor->selection.has_value();
    m.clip_width = m_editor->clip.width;
    m.clip_height = m_editor->clip.height;
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
    if (m_editor->stroke) {
        editor_end_stroke();  // a key while the button is held (undo, save)
    }
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
        case Kind::Terrain:
            editor_terrain_request(request);
            break;
        case Kind::Objects:
            editor_objects_request(request);
            break;
        case Kind::RenameCity:
        case Kind::CityGoods:
        case Kind::CityOwner:
            editor_city_request(request);
            break;
        case Kind::TerrainCopy: {
            const auto& sel = m_editor->selection;
            if (!sel) {
                editor_notice(ui::tr("editor.terrain.refused.no_copy"), true, "nothing selected");
                break;
            }
            m_editor->clip =
                editor::copy_terrain(m_state->world, m_buildings, sel->island, sel->x0, sel->y0, sel->x1, sel->y1);
            editor_notice(
                ui::tr("editor.terrain.copied", {number(m_editor->clip.width), number(m_editor->clip.height)}), false,
                "terrain copied");
            break;
        }
        case Kind::TerrainTurn:
            m_editor->clip = editor::turned(m_editor->clip, m_buildings);
            break;
        case Kind::TerrainMirror:
            m_editor->clip = editor::mirrored(m_editor->clip, m_buildings);
            break;
        case Kind::MovePlaceholder:
        case Kind::RemovePlaceholder:
        case Kind::ChangePlaceholder:
            editor_placeholder_request(request);
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
                editor_load_island(m_editor_files[request.index],
                                   new_island_climate(m_editor->document.world(), request.island.climate).first);
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

/// Whether the panel's page is about what is picked: the selection, or a selected island's fertility.
bool shows_pick(EditorPanel::Page page) {
    return page == EditorPanel::Page::Selection || page == EditorPanel::Page::Fertility ||
           page == EditorPanel::Page::CityName || page == EditorPanel::Page::CityGoods;
}

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
    if (request.kind == Kind::PlaceIsland && request.island.placeholder) {
        Placing p;
        p.kind = Placing::Kind::NewPlaceholder;
        p.placeholder.climate = static_cast<std::uint8_t>(request.island.climate & 1U);
        p.placeholder.size = request.island.size;
        std::tie(p.width, p.height) = editor::placeholder_size(p.placeholder);
        p.page = EditorPanel::Page::Islands;
        m_editor->placing = std::move(p);
        editor_notice(ui::tr("editor.placeholder.placing"), false, "placing a placeholder");
        return;
    }
    if (request.kind == Kind::PlaceIsland) {
        const auto number = world.free_island_number();
        editor::NewIsland wanted;
        wanted.shape = request.island.own_shape ? editor::NewIsland::Shape::Own : editor::NewIsland::Shape::Template;
        std::tie(wanted.climate, wanted.own_climate) = new_island_climate(world, request.island.climate);
        wanted.size = request.island.size;
        wanted.seed = request.island.seed;
        wanted.package_islands = m_options.world.package_islands;
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

void MapViewer::editor_placeholder_request(const EditorRequest& request) {
    using Kind = EditorRequest::Kind;
    const sim::World& world = m_editor->document.world();
    if (!editor::placeholder_open(world, request.index)) {
        return;
    }
    const assets::ScenarioPlan::Island& now = world.planned_islands()->plan.islands[request.index];
    const std::string name = number(request.index + 1);
    switch (request.kind) {
        case Kind::MovePlaceholder: {
            EditorSession::Placing p;
            p.kind = EditorSession::Placing::Kind::MovePlaceholder;
            p.index = request.index;
            p.placeholder = now;
            std::tie(p.width, p.height) = editor::placeholder_size(now);
            p.page = EditorPanel::Page::Selection;
            m_editor->placing = std::move(p);
            editor_notice(ui::tr("editor.placeholder.placing"), false, "placing a placeholder");
            return;
        }
        case Kind::RemovePlaceholder:
            if (editor_perform(editor::remove_placeholder(world, request.index))) {
                m_editor->pick = EditorPick{};
                if (m_hud && m_hud->editor_panel() != nullptr) {
                    m_hud->editor_panel()->show_document();
                }
                editor_notice(ui::tr("editor.placeholder.removed", {name}), false, "placeholder deleted");
            }
            return;
        case Kind::ChangePlaceholder: {
            assets::ScenarioPlan::Island changed = now;
            changed.climate = request.island.climate;
            changed.size = request.island.size;
            auto op = editor::change_placeholder(world, request.index, changed);
            // A size class without room is passed over, in the direction the button went.
            const bool forward = (request.island.size + 5 - now.size) % 5 == 1;
            for (int tries = 0; !op && request.island.size != now.size && tries < 3; ++tries) {
                changed.size = static_cast<std::uint8_t>((changed.size + (forward ? 1 : 4)) % 5);
                if (changed.size == now.size) {
                    break;
                }
                op = editor::change_placeholder(world, request.index, changed);
            }
            if (!op) {
                editor_notice(ui::tr("editor.placeholder.no_room"), true, op.error().message);
                return;
            }
            m_editor->document.perform(std::move(op).value());
            return;
        }
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

void MapViewer::editor_load_island(const std::filesystem::path& file, std::uint8_t climate) {
    auto bytes = core::read_file(file);
    std::vector<std::string> warnings;
    auto island = bytes ? editor::load_island(*bytes, m_buildings, &m_game, &warnings, climate)
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
    // The land units as they stand now: the editor's world does not run, so nothing else draws
    // or finds a unit placed, taken away or brought back by undo.
    m_soldiers.reset(m_state->world, m_buildings, m_figures);
    m_camera.set_world_size(m_state->world.width(), m_state->world.height());
    if (m_hud) {
        m_hud->rebuild_minimap();
    }
    const std::size_t islands = m_state->world.islands().size();
    EditorPick& pick = m_editor->pick;
    if ((pick.kind == EditorPick::Kind::Island || pick.kind == EditorPick::Kind::Building) && pick.island >= islands) {
        pick = EditorPick{};
    }
    if (pick.kind == EditorPick::Kind::Placeholder && !editor::placeholder_open(m_state->world, pick.island)) {
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
    if (p.kind == Kind::NewPlaceholder || p.kind == Kind::MovePlaceholder) {
        const bool moving = p.kind == Kind::MovePlaceholder;
        const std::optional<std::size_t> own = moving ? std::optional<std::size_t>(p.index) : std::nullopt;
        if (!editor::free_for_placeholder(world, x, y, p.width, p.height, own)) {
            editor_notice(ui::tr("editor.placeholder.no_room"), true, "no room for the placeholder");
            return true;
        }
        const auto ux = static_cast<std::uint32_t>(x);
        const auto uy = static_cast<std::uint32_t>(y);
        const std::size_t index = moving ? p.index : world.planned_islands()->plan.islands.size();
        assets::ScenarioPlan::Island moved = p.placeholder;
        moved.x = ux;
        moved.y = uy;
        if (!editor_perform(moving
                                ? editor::change_placeholder(world, p.index, moved)
                                : editor::add_placeholder(world, p.placeholder.climate, p.placeholder.size, ux, uy))) {
            return true;
        }
        m_editor->placing.reset();
        EditorPick pick;
        pick.kind = EditorPick::Kind::Placeholder;
        pick.island = static_cast<std::uint16_t>(index);
        pick.x = tile.x;
        pick.y = tile.y;
        if (m_hud && m_hud->editor_panel() != nullptr && m_hud->editor_panel()->show_selection()) {
            m_editor->pick = pick;
        }
        editor_notice(ui::tr(moving ? "editor.placeholder.moved" : "editor.placeholder.placed", {number(index + 1)}),
                      false, moving ? "placeholder moved" : "placeholder placed");
        return true;
    }
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
        case Kind::NewPlaceholder:
        case Kind::MovePlaceholder:
            break;  // above
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
        // Open sea in a placeholder's rectangle: the placeholder.
        if (pick.kind == EditorPick::Kind::Sea) {
            if (const auto placeholder = editor::placeholder_at(m_editor->document.world(), pick.x, pick.y)) {
                m_editor->pick.kind = EditorPick::Kind::Placeholder;
                m_editor->pick.island = static_cast<std::uint16_t>(*placeholder);
            }
        }
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
        case Kind::Placeholder: {
            if (!editor::placeholder_open(world, pick.island)) {
                return;
            }
            const assets::ScenarioPlan::Island& e = world.planned_islands()->plan.islands[pick.island];
            const auto [w, h] = editor::placeholder_size(e);
            m.selection = ui::tr("editor.placeholder.name", {number(pick.island + 1U)});
            add("editor.prop.position", tiles(e.x, e.y));
            add("editor.prop.size", ui::tr("editor.size", {number(w), number(h)}));
            add("editor.prop.climate", ui::tr(e.climate == 0   ? "editor.prop.north"
                                              : e.climate == 1 ? "editor.prop.south"
                                                               : "editor.placeholder.by_place"));
            add("editor.placeholder.template", e.template_number == assets::ScenarioPlan::kRandomTemplate
                                                   ? ui::tr("editor.placeholder.random")
                                                   : number(e.template_number));
            return;
        }
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
                const sim::Climate* own = sim::island_climate(world.balance().climates, island);
                add("editor.prop.climate", own != nullptr                   ? own->name(ui::current_language())
                                           : island.header.climate % 2 == 0 ? ui::tr("editor.prop.north")
                                                                            : ui::tr("editor.prop.south"));
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
    const auto screen = [&](double x, double y) {
        const auto [px, py] = render::text_figure_point(view, x, y, 0.0);
        double sx = 0.0;
        double sy = 0.0;
        m_camera.world_to_screen(px, py, sx, sy);
        return std::pair<double, double>{sx, sy};
    };
    // A rectangle of the map as a dotted line: a dot of 3 x 3 pixels every 6.
    const auto outline = [&](std::int64_t x0, std::int64_t y0, double w, double h, std::int32_t ink) {
        const auto x = static_cast<double>(x0);
        const auto y = static_cast<double>(y0);
        const std::array<std::pair<double, double>, 4> corners = {screen(x, y), screen(x + w, y), screen(x + w, y + h),
                                                                  screen(x, y + h)};
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
    };
    // The placeholders of the world plan: islands made when a game starts, in yellow.
    using PlacingKind = EditorSession::Placing::Kind;
    if (const auto& planned = world.planned_islands()) {
        const std::int32_t ink = ui::rgb_ink(assets::Rgba{240, 210, 70, 255});
        for (std::size_t i = 0; i < planned->plan.islands.size(); ++i) {
            const bool in_hand = m_editor->placing && m_editor->placing->kind == PlacingKind::MovePlaceholder &&
                                 m_editor->placing->index == i;
            if (in_hand || !editor::placeholder_open(world, i)) {
                continue;
            }
            const assets::ScenarioPlan::Island& e = planned->plan.islands[i];
            const auto [w, h] = editor::placeholder_size(e);
            outline(e.x, e.y, w, h, ink);
            text_at(e.x + w / 2.0, e.y + h / 2.0, ui::tr("editor.placeholder.name", {number(i + 1)}));
        }
    }
    // The island being placed: its outline under the mouse, green where it fits, red where not.
    if (m_editor->placing) {
        if (const auto tile = m_input->tile_at_mouse()) {
            const EditorSession::Placing& p = *m_editor->placing;
            const auto [x0, y0] = editor_place_origin(*tile);
            bool fits = false;
            if (p.kind == PlacingKind::NewPlaceholder || p.kind == PlacingKind::MovePlaceholder) {
                const std::optional<std::size_t> own =
                    p.kind == PlacingKind::MovePlaceholder ? std::optional<std::size_t>(p.index) : std::nullopt;
                fits = editor::free_for_placeholder(world, x0, y0, p.width, p.height, own);
            } else {
                const std::optional<std::size_t> except =
                    p.kind == PlacingKind::Move ? std::optional<std::size_t>(p.index) : std::nullopt;
                fits = editor::free_place(world, x0, y0, p.width, p.height, except) &&
                       !editor::ship_in_way(world, x0, y0, p.width, p.height, except);
            }
            outline(x0, y0, p.width, p.height,
                    ui::rgb_ink(fits ? assets::Rgba{90, 230, 90, 255} : assets::Rgba{235, 70, 60, 255}));
        }
    }
    // The terrain tool under the mouse: the tiles it lays, or the tile it starts from.
    const EditorPanel* panel = m_hud->editor_panel();
    if (panel != nullptr && panel->page() == EditorPanel::Page::Terrain && !m_editor->placing &&
        !m_hud->covers(static_cast<std::int32_t>(m_input->mouse_x()), static_cast<std::int32_t>(m_input->mouse_y()))) {
        if (const auto tile = m_input->tile_at_mouse()) {
            using Tool = EditorTerrain::Tool;
            const EditorTerrain& t = m_editor->terrain;
            std::uint32_t w = 1;
            std::uint32_t h = 1;
            const assets::BuildingType* b = editor_terrain_piece();
            if (t.tool == Tool::Paste && !m_editor->clip.empty()) {
                w = m_editor->clip.width;
                h = m_editor->clip.height;
            } else if (t.tool == Tool::Random || t.tool == Tool::Land || t.tool == Tool::Water ||
                       t.tool == Tool::Coast ||
                       (t.tool == Tool::Brush && b != nullptr && b->size_x == 1 && b->size_y == 1)) {
                w = h = editor::kBrushSizes[t.brush % editor::kBrushSizes.size()];
            } else if (t.tool == Tool::Brush && b != nullptr) {
                sim::rotated_size(*b, t.rotation, w, h);
            }
            const std::int64_t x0 = std::int64_t{tile->x} - (std::int64_t{w} - 1) / 2;
            const std::int64_t y0 = std::int64_t{tile->y} - (std::int64_t{h} - 1) / 2;
            if (t.tool != Tool::Select) {
                // A copy that cannot go where the mouse is: its frame in red, the map unchanged.
                const bool refused = t.tool == Tool::Paste && m_editor->paste_refused;
                outline(x0, y0, w, h,
                        ui::rgb_ink(refused ? assets::Rgba{230, 60, 60, 255} : assets::Rgba{250, 250, 250, 255}));
            }
        }
    }
    // The building under the mouse on the page "Buildings": green where it can stand, red where
    // not; the tile to take away in white.
    if (panel != nullptr && panel->page() == EditorPanel::Page::Objects && !m_editor->placing &&
        !m_hud->covers(static_cast<std::int32_t>(m_input->mouse_x()), static_cast<std::int32_t>(m_input->mouse_y()))) {
        if (const auto tile = m_input->tile_at_mouse()) {
            const assets::BuildingType* b = editor_object_piece();
            const EditorObjects::Tool tool = m_editor->objects.tool;
            if (tool == EditorObjects::Tool::Ship || tool == EditorObjects::Tool::Unit) {
                // A ship or unit: its tile, green where it can go.
                const bool fits =
                    (tool == EditorObjects::Tool::Ship
                         ? editor::ship_fits(world, m_buildings, tile->x, tile->y)
                         : editor::unit_fits(world, m_buildings, tile->x, tile->y)) == editor::UnitRefusal::None;
                outline(tile->x, tile->y, 1, 1,
                        ui::rgb_ink(fits ? assets::Rgba{90, 230, 90, 255} : assets::Rgba{230, 60, 60, 255}));
            } else if (tool == EditorObjects::Tool::Resource) {
                // A vein, treasure or deer: its tile, green where it can go.
                bool fits = false;
                if (world.contains(static_cast<std::uint32_t>(tile->x), static_cast<std::uint32_t>(tile->y))) {
                    const sim::World::Cell& c =
                        world.cell(static_cast<std::uint32_t>(tile->x), static_cast<std::uint32_t>(tile->y));
                    fits =
                        c.island != sim::World::kSea &&
                        editor::resource_fits(
                            world, m_buildings, c.island,
                            {static_cast<std::int32_t>(c.local_x), static_cast<std::int32_t>(c.local_y)},
                            static_cast<editor::Resource>(std::min(
                                m_editor->objects.resource, editor::kResources - 1))) == editor::ResourceRefusal::None;
                }
                outline(tile->x, tile->y, 1, 1,
                        ui::rgb_ink(fits ? assets::Rgba{90, 230, 90, 255} : assets::Rgba{230, 60, 60, 255}));
            } else if (tool == EditorObjects::Tool::Remove || b == nullptr) {
                outline(tile->x, tile->y, 1, 1, ui::rgb_ink(assets::Rgba{250, 250, 250, 255}));
            } else {
                std::uint32_t w = 1;
                std::uint32_t h = 1;
                sim::rotated_size(*b, m_editor->objects.rotation, w, h);
                std::int64_t x0 = std::int64_t{tile->x} - (std::int64_t{w} - 1) / 2;
                std::int64_t y0 = std::int64_t{tile->y} - (std::int64_t{h} - 1) / 2;
                bool fits = false;
                if (const auto order = editor_object_order(tile->x, tile->y)) {
                    // Where a click would put it: a Kontor finds its coast near the mouse.
                    const sim::BuildPlan plan = sim::plan_build(world, m_buildings, *order);
                    fits = plan.placed > 0;
                    const sim::PlannedBuild& p = plan.items.front();
                    if (fits && p.type != nullptr) {
                        const sim::WorldIsland& isl = world.islands()[order->island];
                        sim::rotated_size(*p.type, p.rotation, w, h);
                        x0 = std::int64_t{isl.x} + p.x;
                        y0 = std::int64_t{isl.y} + p.y;
                    }
                }
                outline(x0, y0, w, h,
                        ui::rgb_ink(fits ? assets::Rgba{90, 230, 90, 255} : assets::Rgba{230, 60, 60, 255}));
            }
        }
    }
    // The terrain selected, in light blue, while the terrain page is open.
    if (panel != nullptr && panel->page() == EditorPanel::Page::Terrain && m_editor->selection &&
        m_editor->selection->island < world.islands().size()) {
        const auto& sel = *m_editor->selection;
        const sim::WorldIsland& isl = world.islands()[sel.island];
        outline(std::int64_t{isl.x} + sel.x0, std::int64_t{isl.y} + sel.y0, sel.x1 - sel.x0 + 1.0,
                sel.y1 - sel.y0 + 1.0, ui::rgb_ink(assets::Rgba{120, 200, 255, 255}));
    }
    // The riches of the islands on the page "Objects": veins, treasures and deer by name, which the
    // map shows nowhere else.
    if (panel != nullptr && panel->page() == EditorPanel::Page::Objects) {
        for (const sim::WorldIsland& island : world.islands()) {
            for (const assets::IslandHeader::Deposit& d : island.header.deposits) {
                const char* key = d.ware == assets::ware::kGold       ? "editor.objects.mark.gold"
                                  : d.ware == assets::ware::kTreasure ? "editor.objects.mark.treasure"
                                                                      : "editor.objects.mark.ore";
                // A vein lies at its mountain's origin: the mark goes a tile into the mountain.
                const double inside = d.ware == assets::ware::kTreasure ? 0.5 : 1.5;
                text_at(island.x + d.x + inside, island.y + d.y + inside, ui::tr(key));
            }
            for (const sim::DeerSpot& spot : island.deer_spots) {
                if (spot.used) {
                    text_at(island.x + spot.x + 0.5, island.y + spot.y + 0.5, ui::tr("editor.objects.mark.deer"));
                }
            }
        }
    }
    // The mark of what the panel shows: a cross on the tile, or over the ship, unit or building.
    const EditorPick& pick = m_editor->pick;
    const bool shown = m_hud->editor_panel() != nullptr && shows_pick(m_hud->editor_panel()->page());
    if (shown) {
        using Kind = EditorPick::Kind;
        const std::string mark = "\xD7";
        switch (pick.kind) {
            case Kind::None:
                break;
            case Kind::Sea:
            case Kind::Island:
            case Kind::Placeholder:
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
    if (m_hud->editor_panel() != nullptr && !shows_pick(m_hud->editor_panel()->page())) {
        m_editor->pick = EditorPick{};
    }
    // The page an island is placed from left: it goes back.
    if (m_editor->placing &&
        (m_hud->editor_panel() == nullptr || m_hud->editor_panel()->page() != m_editor->placing->page)) {
        m_editor->placing.reset();
    }
    m_hud->set_editor_model(editor_model());
}

namespace {

static_assert(kTerrainBrushes == editor::kBrushSizes, "the panel's brushes are the editor's");

/// The note of why a terrain tool changed nothing.
std::string terrain_refusal(editor::TerrainStroke::Refusal refusal) {
    using Refusal = editor::TerrainStroke::Refusal;
    switch (refusal) {
        case Refusal::Free:
            return ui::tr("editor.terrain.unchanged");
        case Refusal::NoIsland:
            return ui::tr("editor.terrain.refused.no_island");
        case Refusal::NotTerrain:
            return ui::tr("editor.terrain.refused.building");
        case Refusal::City:
            return ui::tr("editor.terrain.refused.city");
        case Refusal::Unit:
            return ui::tr("editor.terrain.refused.unit");
        case Refusal::RunningGame:
            return ui::tr("editor.islands.in_use.game");
        case Refusal::Edge:
            return ui::tr("editor.terrain.refused.edge");
        case Refusal::Level:
            return ui::tr("editor.terrain.refused.level");
        case Refusal::Shape:
            return ui::tr("editor.terrain.refused.shape");
    }
    return {};
}

/// A number for a tile of a stroke: the random fill's pieces.
std::uint64_t tile_seed(std::uint64_t stroke, std::int64_t x, std::int64_t y) noexcept {
    return stroke * 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(x) << 32 ^ static_cast<std::uint64_t>(y);
}

}  // namespace

const assets::BuildingType* MapViewer::editor_terrain_piece() const {
    if (!m_editor) {
        return nullptr;
    }
    const auto pieces = editor::terrain_pieces(m_buildings, static_cast<editor::TerrainGroup>(m_editor->terrain.group));
    if (pieces.empty()) {
        return nullptr;
    }
    return pieces[std::min(m_editor->terrain.piece, pieces.size() - 1)];
}

void MapViewer::editor_terrain_request(const EditorRequest& request) {
    EditorTerrain t = request.terrain;
    t.group = static_cast<std::uint8_t>(t.group % editor::kTerrainGroups);
    t.rotation &= 3U;
    t.brush = static_cast<std::uint8_t>(t.brush % editor::kBrushSizes.size());
    const auto pieces = editor::terrain_pieces(m_buildings, static_cast<editor::TerrainGroup>(t.group));
    t.piece = pieces.empty() ? 0 : std::min(t.piece, pieces.size() - 1);
    if (!pieces.empty() && pieces[t.piece]->rotate == 0) {
        t.rotation = 0;
    }
    m_editor->terrain = t;
    m_editor->notice.clear();  // the note of the tool chosen shows again
}

void MapViewer::editor_pipette(std::uint32_t x, std::uint32_t y) {
    const auto piece = editor::terrain_at(m_state->world, m_buildings, x, y);
    const assets::BuildingType* b = piece ? m_buildings.by_id(piece->id) : nullptr;
    const auto group = b != nullptr ? editor::terrain_group(m_buildings, *b) : std::nullopt;
    if (!group) {
        editor_notice(ui::tr("editor.terrain.pipette_none"), true, "no terrain there");
        return;
    }
    const auto pieces = editor::terrain_pieces(m_buildings, *group);
    const auto it = std::find(pieces.begin(), pieces.end(), b);
    EditorTerrain& t = m_editor->terrain;
    t.group = static_cast<std::uint8_t>(*group);
    t.piece = static_cast<std::size_t>(it - pieces.begin());
    t.rotation = b->rotate > 0 ? piece->rotation : 0;
    t.tool = EditorTerrain::Tool::Brush;
    editor_notice(ui::tr("editor.terrain.picked", {number(b->id)}), false, "terrain picked");
}

void MapViewer::editor_paint(editor::TerrainStroke& stroke, std::int64_t x, std::int64_t y, bool going_on) {
    const EditorTerrain& t = m_editor->terrain;
    const std::uint32_t side = editor::kBrushSizes[t.brush % editor::kBrushSizes.size()];
    const sim::World& world = m_state->world;
    if (t.tool == EditorTerrain::Tool::Coast) {
        // The coast under the brush laid anew, island by island.
        std::vector<std::pair<std::uint16_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>>> by_island;
        for (const auto& [tx, ty] : editor::brush_tiles(x, y, side, world.width(), world.height())) {
            const sim::World::Cell& c = world.cell(tx, ty);
            if (c.island == sim::World::kSea) {
                continue;
            }
            auto it =
                std::find_if(by_island.begin(), by_island.end(), [&](const auto& e) { return e.first == c.island; });
            if (it == by_island.end()) {
                by_island.emplace_back(c.island, std::vector<std::pair<std::uint32_t, std::uint32_t>>{});
                it = by_island.end() - 1;
            }
            it->second.emplace_back(c.local_x, c.local_y);
        }
        if (by_island.empty()) {
            stroke.note(editor::TerrainStroke::Refusal::NoIsland);
        }
        for (const auto& [island, tiles] : by_island) {
            (void)stroke.tidy_coast(island, tiles, tile_seed(m_editor->strokes, x, y));
        }
        return;
    }
    if (t.tool == EditorTerrain::Tool::Land || t.tool == EditorTerrain::Tool::Water) {
        const auto brush = editor::brush_tiles(x, y, side, world.width(), world.height());
        if (brush.empty()) {
            return;
        }
        // The stroke drawn (not the preview's): land may first need room, which ends it and starts
        // another.
        const bool live = &stroke == m_editor->stroke.get();
        if (t.tool == EditorTerrain::Tool::Land && live) {
            const std::int64_t half = (std::int64_t{side} - 1) / 2;
            editor_make_room(x - half, y - half, x - half + side - 1, y - half + side - 1);
            if (!m_editor->stroke) {
                return;
            }
        }
        editor::TerrainStroke& into = live ? *m_editor->stroke : stroke;
        // Land close to another island's land: the two were not joined (one has cities or
        // buildings), so the brush stops short of it and open sea stays between them.
        constexpr std::int64_t kApart = 4;  // dark sea between the two surfs
        const auto by_other = [&](std::uint32_t tx, std::uint32_t ty, std::uint16_t own) {
            const std::int64_t W = world.width();
            const std::int64_t H = world.height();
            for (std::int64_t ny = std::max<std::int64_t>(0, std::int64_t{ty} - kApart);
                 ny <= std::min<std::int64_t>(H - 1, std::int64_t{ty} + kApart); ++ny) {
                for (std::int64_t nx = std::max<std::int64_t>(0, std::int64_t{tx} - kApart);
                     nx <= std::min<std::int64_t>(W - 1, std::int64_t{tx} + kApart); ++nx) {
                    const sim::World::Cell& at =
                        world.cell(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
                    if (at.island == sim::World::kSea || at.island == own ||
                        (world.island_use(at.island) == sim::World::IslandUse::None &&
                         world.island_use(own) == sim::World::IslandUse::None)) {
                        continue;  // two islands without cities are joined instead
                    }
                    const sim::MapCell& top = world.islands()[at.island].map.top(at.local_x, at.local_y);
                    const assets::BuildingType* b = top.empty() ? nullptr : m_buildings.by_sprite(top.sprite);
                    if (b != nullptr && editor::terrain_level(*b) > 0) {
                        return true;
                    }
                }
            }
            return false;
        };
        // The brush's tiles by island; the coast about them is laid anew.
        std::vector<std::pair<std::uint16_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>>> by_island;
        bool kept_apart = false;
        for (const auto& [tx, ty] : brush) {
            const sim::World::Cell& c = world.cell(tx, ty);
            if (c.island == sim::World::kSea) {
                continue;
            }
            if (t.tool == EditorTerrain::Tool::Land && by_other(tx, ty, c.island)) {
                kept_apart = true;
                continue;
            }
            auto it =
                std::find_if(by_island.begin(), by_island.end(), [&](const auto& e) { return e.first == c.island; });
            if (it == by_island.end()) {
                by_island.emplace_back(c.island, std::vector<std::pair<std::uint32_t, std::uint32_t>>{});
                it = by_island.end() - 1;
            }
            it->second.emplace_back(c.local_x, c.local_y);
        }
        if (by_island.empty() && !kept_apart) {
            into.note(editor::TerrainStroke::Refusal::NoIsland);
        }
        for (const auto& [island, tiles] : by_island) {
            (void)into.shape(island, tiles, t.tool == EditorTerrain::Tool::Land, tile_seed(m_editor->strokes, x, y));
        }
        return;
    }
    if (t.tool == EditorTerrain::Tool::Random) {
        for (const auto& [tx, ty] : editor::brush_tiles(x, y, side, world.width(), world.height())) {
            const sim::World::Cell& c = world.cell(tx, ty);
            const std::uint8_t climate = c.island != sim::World::kSea ? world.islands()[c.island].header.climate : 0;
            const auto piece = editor::random_piece(m_buildings, static_cast<editor::TerrainGroup>(t.group), climate,
                                                    tile_seed(m_editor->strokes, tx, ty));
            if (const assets::BuildingType* b = piece ? m_buildings.by_id(piece->id) : nullptr) {
                (void)stroke.stamp_world(tx, ty, *b, piece->rotation);
            }
        }
        return;
    }
    const assets::BuildingType* b = editor_terrain_piece();
    if (b == nullptr) {
        return;
    }
    if (b->size_x == 1 && b->size_y == 1) {
        for (const auto& [tx, ty] : editor::brush_tiles(x, y, side, world.width(), world.height())) {
            (void)stroke.stamp_world(tx, ty, *b, t.rotation);
        }
        return;
    }
    // A piece of several tiles about the mouse; dragged, the next one where it no longer covers the last.
    std::uint32_t fw = 0;
    std::uint32_t fh = 0;
    sim::rotated_size(*b, t.rotation, fw, fh);
    const std::int64_t ox = x - (std::int64_t{fw} - 1) / 2;
    const std::int64_t oy = y - (std::int64_t{fh} - 1) / 2;
    if (going_on && m_editor->stroke_large && std::abs(ox - m_editor->stroke_large->first) < fw &&
        std::abs(oy - m_editor->stroke_large->second) < fh) {
        return;
    }
    if (stroke.stamp_world(ox, oy, *b, t.rotation) && &stroke == m_editor->stroke.get()) {
        m_editor->stroke_large = {ox, oy};
    }
}

void MapViewer::editor_make_room(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
    const sim::World& world = m_state->world;
    const auto W = static_cast<std::int64_t>(world.width());
    const auto H = static_cast<std::int64_t>(world.height());
    constexpr std::int64_t kRoom = 2;   // a tile of surf and one of water beyond the land
    constexpr std::int64_t kMore = 12;  // grown further, so that a stroke does not grow it at every tile
    constexpr std::int64_t kNear = 6;   // land painted this close to an island's land grows it
    const std::int64_t nx0 = std::max<std::int64_t>(0, x0 - kRoom);
    const std::int64_t ny0 = std::max<std::int64_t>(0, y0 - kRoom);
    const std::int64_t nx1 = std::min<std::int64_t>(W - 1, x1 + kRoom);
    const std::int64_t ny1 = std::min<std::int64_t>(H - 1, y1 + kRoom);
    if (nx0 > nx1 || ny0 > ny1) {
        return;
    }
    const std::int64_t cx = std::clamp<std::int64_t>((x0 + x1) / 2, 0, W - 1);
    const std::int64_t cy = std::clamp<std::int64_t>((y0 + y1) / 2, 0, H - 1);
    std::optional<std::size_t> island;
    if (const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy));
        c.island != sim::World::kSea) {
        island = c.island;
    } else {
        // The island whose land is nearest, within kNear tiles of the brush (not of its rectangle,
        // whose sea may reach far out).
        std::int64_t best = kNear + 1;
        for (std::int64_t y = std::max<std::int64_t>(0, cy - kNear); y <= std::min(H - 1, cy + kNear); ++y) {
            for (std::int64_t x = std::max<std::int64_t>(0, cx - kNear); x <= std::min(W - 1, cx + kNear); ++x) {
                const sim::World::Cell& at = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                if (at.island == sim::World::kSea) {
                    continue;
                }
                const sim::MapCell& top = world.islands()[at.island].map.top(at.local_x, at.local_y);
                const assets::BuildingType* b = top.empty() ? nullptr : m_buildings.by_sprite(top.sprite);
                if (b == nullptr || editor::terrain_level(*b) == 0) {
                    continue;  // water
                }
                const std::int64_t d = std::max(std::abs(x - cx), std::abs(y - cy));
                if (d < best) {
                    best = d;
                    island = at.island;
                }
            }
        }
    }
    const auto restart = [&](std::unique_ptr<editor::Operation> room) {
        // The stroke so far and the room are done already; the stroke's end keeps them as one step.
        if (std::unique_ptr<editor::Operation> drawn = m_editor->stroke->finish("terrain")) {
            m_editor->stroke_steps.push_back(std::move(drawn));
        }
        room->apply(m_state->world);
        m_editor->stroke_steps.push_back(std::move(room));
        editor_islands_changed();
        m_editor->stroke = std::make_unique<editor::TerrainStroke>(m_state->world, m_buildings);
    };
    // Set when land of another island lies close to the brush but one of the two has cities or
    // buildings: such islands keep their shape, so they are not joined, and the notice says so.
    std::optional<std::size_t> unjoined;
    if (island) {
        // Land of another island close to the brush: both become one island first, so that the
        // coast between them is laid as one.
        constexpr std::int64_t kJoin = 3;
        const bool free = world.island_use(*island) == sim::World::IslandUse::None;
        std::optional<std::size_t> other;
        for (std::int64_t y = std::max<std::int64_t>(0, y0 - kJoin); y <= std::min(H - 1, y1 + kJoin) && !other; ++y) {
            for (std::int64_t x = std::max<std::int64_t>(0, x0 - kJoin); x <= std::min(W - 1, x1 + kJoin); ++x) {
                const sim::World::Cell& at = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                if (at.island == sim::World::kSea || at.island == *island) {
                    continue;
                }
                const sim::MapCell& top = world.islands()[at.island].map.top(at.local_x, at.local_y);
                const assets::BuildingType* b = top.empty() ? nullptr : m_buildings.by_sprite(top.sprite);
                if (b == nullptr || editor::terrain_level(*b) == 0) {
                    continue;
                }
                if (free && world.island_use(at.island) == sim::World::IslandUse::None) {
                    other = at.island;
                    break;
                }
                if (!unjoined) {
                    unjoined = at.island;
                }
            }
        }
        if (other) {
            auto op = editor::merge_islands(m_editor->document.world(), *island, *other, m_buildings);
            if (op) {
                const std::size_t kept = *island > *other ? *island - 1 : *island;
                restart(std::move(op).value());
                editor_make_room(x0, y0, x1, y1);  // the joined island may still need room
                if (!m_editor->stroke) {
                    return;
                }
                editor_notice(ui::tr("editor.terrain.joined", {number(kept + 1)}), false, "islands joined");
                return;
            }
            std::fprintf(stderr, "editor: islands not joined: %s\n", op.error().message.c_str());
        }
    }
    if (island) {
        const sim::WorldIsland& isl = world.islands()[*island];
        const std::int64_t ix0 = isl.x;
        const std::int64_t iy0 = isl.y;
        const std::int64_t ix1 = ix0 + isl.map.width() - 1;
        const std::int64_t iy1 = iy0 + isl.map.height() - 1;
        const auto refuse_join = [&] {
            if (unjoined) {
                const std::size_t built =
                    world.island_use(*island) != sim::World::IslandUse::None ? *island : *unjoined;
                std::string text = ui::tr("editor.terrain.refused.join", {number(built + 1)});
                if (m_editor->notice != text) {  // a stroke asks again at every step
                    editor_notice(std::move(text), true, "islands not joined");
                }
            }
        };
        if (nx0 >= ix0 && ny0 >= iy0 && nx1 <= ix1 && ny1 <= iy1) {
            refuse_join();
            return;  // room enough
        }
        if (world.island_use(*island) != sim::World::IslandUse::None) {
            refuse_join();
            return;  // an island with cities keeps its size; the brush stops at its edge
        }
        const auto grow = [&](std::int64_t more) {
            const auto side = [](std::int64_t need, std::int64_t room) {
                return static_cast<std::uint32_t>(std::clamp<std::int64_t>(need, 0, room));
            };
            const std::uint32_t left = nx0 < ix0 ? side(ix0 - nx0 + more, ix0) : 0;
            const std::uint32_t top = ny0 < iy0 ? side(iy0 - ny0 + more, iy0) : 0;
            const std::uint32_t right = nx1 > ix1 ? side(nx1 - ix1 + more, W - 1 - ix1) : 0;
            const std::uint32_t bottom = ny1 > iy1 ? side(ny1 - iy1 + more, H - 1 - iy1) : 0;
            return editor::grow_island(m_editor->document.world(), *island, left, top, right, bottom, m_buildings);
        };
        auto op = grow(kMore);
        if (!op) {
            op = grow(0);
        }
        if (!op) {
            editor_notice(ui::tr("editor.terrain.no_growth"), true, op.error().message);
            return;
        }
        restart(std::move(op).value());
        if (unjoined) {
            refuse_join();
        } else {
            editor_notice(ui::tr("editor.terrain.grown", {number(*island + 1)}), false, "island grown");
        }
        return;
    }
    // Open sea: a new island about the brush, as large as the smallest the editor makes.
    constexpr std::int64_t kSide = sim::shapes::kMinSide;
    std::int64_t sx0 = nx0 - kMore;
    std::int64_t sy0 = ny0 - kMore;
    std::int64_t sx1 = std::max(nx1 + kMore, sx0 + kSide - 1);
    std::int64_t sy1 = std::max(ny1 + kMore, sy0 + kSide - 1);
    sx0 = std::max<std::int64_t>(0, sx0);
    sy0 = std::max<std::int64_t>(0, sy0);
    sx1 = std::min<std::int64_t>(W - 1, sx1);
    sy1 = std::min<std::int64_t>(H - 1, sy1);
    const std::uint8_t climate = 0;
    auto op = editor::add_sea_island(m_editor->document.world(), static_cast<std::uint32_t>(sx0),
                                     static_cast<std::uint32_t>(sy0), static_cast<std::uint32_t>(sx1 - sx0 + 1),
                                     static_cast<std::uint32_t>(sy1 - sy0 + 1), climate, m_buildings);
    if (!op) {
        editor_notice(ui::tr("editor.terrain.no_growth"), true, op.error().message);
        return;
    }
    restart(std::move(op).value());
    editor_notice(ui::tr("editor.terrain.new_island", {number(m_state->world.islands().size())}), false, "island made");
}

void MapViewer::editor_end_stroke() {
    if (!m_editor || !m_editor->stroke) {
        return;
    }
    const editor::TerrainStroke::Refusal refused = m_editor->stroke->first_refusal();
    std::unique_ptr<editor::Operation> op =
        m_editor->stroke->finish(m_editor->terrain.tool == EditorTerrain::Tool::Coast ? "terrain_coast" : "terrain");
    m_editor->stroke.reset();
    m_editor->stroke_at.reset();
    m_editor->stroke_large.reset();
    if (!m_editor->stroke_steps.empty()) {
        if (op) {
            m_editor->stroke_steps.push_back(std::move(op));
        }
        m_editor->document.record(std::make_unique<editor::Steps>(std::move(m_editor->stroke_steps), "terrain"));
        m_editor->stroke_steps.clear();
    } else if (op) {
        m_editor->document.perform(std::move(op));
        m_editor->notice.clear();
    } else if (refused != editor::TerrainStroke::Refusal::Free) {
        editor_notice(terrain_refusal(refused), true, "terrain refused");
    }
}

bool MapViewer::editor_select(std::int64_t x, std::int64_t y) {
    const sim::World& world = m_state->world;
    if (!m_editor->select_from) {
        return false;
    }
    const auto [fx, fy] = *m_editor->select_from;
    const auto inside = [&](std::int64_t tx, std::int64_t ty) {
        return tx >= 0 && ty >= 0 && tx < world.width() && ty < world.height();
    };
    if (!inside(fx, fy) || !inside(x, y)) {
        m_editor->selection.reset();
        return false;
    }
    const sim::World::Cell& a = world.cell(static_cast<std::uint32_t>(fx), static_cast<std::uint32_t>(fy));
    const sim::World::Cell& b = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (a.island == sim::World::kSea || a.island != b.island) {
        m_editor->selection.reset();
        return false;
    }
    m_editor->selection = EditorSession::TerrainSelection{
        a.island, std::min<std::uint32_t>(a.local_x, b.local_x), std::min<std::uint32_t>(a.local_y, b.local_y),
        std::max<std::uint32_t>(a.local_x, b.local_x), std::max<std::uint32_t>(a.local_y, b.local_y)};
    return true;
}

void MapViewer::editor_paste(std::int64_t x, std::int64_t y) {
    const editor::TerrainClip& clip = m_editor->clip;
    if (clip.empty()) {
        editor_notice(ui::tr("editor.terrain.refused.no_copy"), true, "nothing copied");
        return;
    }
    const sim::World& world = m_state->world;
    if (x < 0 || y < 0 || x >= world.width() || y >= world.height()) {
        return;
    }
    const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (c.island == sim::World::kSea) {
        editor_notice(ui::tr("editor.terrain.refused.no_island"), true, "paste on the open sea");
        return;
    }
    // The copy's middle under the mouse, on the island there.
    const std::int64_t lx = std::int64_t{c.local_x} - (std::int64_t{clip.width} - 1) / 2;
    const std::int64_t ly = std::int64_t{c.local_y} - (std::int64_t{clip.height} - 1) / 2;
    ++m_editor->strokes;
    editor::TerrainStroke stroke(m_state->world, m_buildings);
    const bool laid = stroke.paste(c.island, lx, ly, clip, tile_seed(m_editor->strokes, x, y));
    const editor::TerrainStroke::Refusal refused = stroke.first_refusal();
    if (!laid) {
        stroke.cancel();
        editor_notice(terrain_refusal(refused), true, "paste refused");
        return;
    }
    if (std::unique_ptr<editor::Operation> op = stroke.finish("terrain_paste")) {
        m_editor->document.perform(std::move(op));
        m_editor->notice.clear();
    }
}

bool MapViewer::editor_stroke(std::optional<render::TilePos> tile, Stroke phase) {
    if (!m_editor) {
        return false;
    }
    const EditorPanel* panel = m_hud ? m_hud->editor_panel() : nullptr;
    if (panel != nullptr && panel->page() == EditorPanel::Page::Objects && !m_editor->placing) {
        editor_end_stroke();
        return editor_objects_stroke(tile, phase);
    }
    editor_end_objects();  // the page left while the button was held
    if (panel == nullptr || panel->page() != EditorPanel::Page::Terrain || m_editor->placing) {
        editor_end_stroke();  // the page left while the button was held
        return false;
    }
    using Tool = EditorTerrain::Tool;
    const Tool tool = m_editor->terrain.tool;
    switch (phase) {
        case Stroke::Begin: {
            editor_end_stroke();
            if (!tile) {
                return true;
            }
            const auto x = static_cast<std::uint32_t>(tile->x);
            const auto y = static_cast<std::uint32_t>(tile->y);
            if (tool == Tool::Pipette) {
                editor_pipette(x, y);
                return true;
            }
            if (tool == Tool::Select) {
                m_editor->select_from = {tile->x, tile->y};
                m_editor->selection.reset();
                (void)editor_select(tile->x, tile->y);
                return true;
            }
            if (tool == Tool::Paste) {
                editor_paste(tile->x, tile->y);
                return true;
            }
            if (tool == Tool::Fill || tool == Tool::Replace) {
                const assets::BuildingType* b = editor_terrain_piece();
                if (b == nullptr) {
                    return true;
                }
                const editor::TerrainPiece piece{b->id, m_editor->terrain.rotation};
                if (const auto clicked = editor::terrain_at(m_state->world, m_buildings, x, y)) {
                    const assets::BuildingType* there = m_buildings.by_id(clicked->id);
                    if (there != nullptr && editor::terrain_level(*there) != editor::terrain_level(*b)) {
                        editor_notice(ui::tr("editor.terrain.refused.level"), true, "terrain of another level");
                        return true;
                    }
                }
                auto op = tool == Tool::Fill ? editor::fill_terrain(m_state->world, m_buildings, x, y, piece)
                                             : editor::replace_terrain(m_state->world, m_buildings, x, y, piece);
                if (op) {
                    m_editor->document.perform(std::move(op).value());
                    m_editor->notice.clear();
                } else {
                    const char* why =
                        op.error().code == core::ErrorCode::Unsupported && (b->size_x != 1 || b->size_y != 1)
                            ? "editor.terrain.refused.large"
                        : op.error().code == core::ErrorCode::NotFound    ? "editor.terrain.refused.no_island"
                        : op.error().code == core::ErrorCode::Unsupported ? "editor.terrain.refused.tiles"
                                                                          : "editor.terrain.unchanged";
                    editor_notice(ui::tr(why), true, op.error().message);
                }
                return true;
            }
            ++m_editor->strokes;
            m_editor->stroke = std::make_unique<editor::TerrainStroke>(m_state->world, m_buildings);
            m_editor->stroke_at = {tile->x, tile->y};
            editor_paint(*m_editor->stroke, tile->x, tile->y, false);
            return true;
        }
        case Stroke::Continue: {
            if (tool == Tool::Select && m_editor->select_from) {
                if (tile) {
                    (void)editor_select(tile->x, tile->y);
                }
                return true;
            }
            if (!m_editor->stroke || !tile || !m_editor->stroke_at) {
                return m_editor->stroke != nullptr;
            }
            const auto [lx, ly] = *m_editor->stroke_at;
            if (lx == tile->x && ly == tile->y) {
                return true;
            }
            const auto line = editor::line_tiles(lx, ly, tile->x, tile->y);
            for (std::size_t i = 1; i < line.size(); ++i) {
                editor_paint(*m_editor->stroke, line[i].first, line[i].second, true);
            }
            m_editor->stroke_at = {tile->x, tile->y};
            return true;
        }
        case Stroke::End:
            if (tool == Tool::Select && m_editor->select_from) {
                m_editor->select_from.reset();
                if (const auto& sel = m_editor->selection) {
                    editor_notice(ui::tr("editor.terrain.selected",
                                         {number(sel->x1 - sel->x0 + 1), number(sel->y1 - sel->y0 + 1)}),
                                  false, "terrain selected");
                } else {
                    editor_notice(ui::tr("editor.terrain.refused.select"), true, "selection not on one island");
                }
                return true;
            }
            editor_end_stroke();
            return true;
    }
    return false;
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
