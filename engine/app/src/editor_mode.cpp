// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor as a mode of the game (ADR 0013): the viewer shows the document's world with the
// game's renderer and frame, the simulation never runs on it, and the side panel below the head is
// the editor's (editor_panel.hpp). The document is always saved as an Open1602 scenario.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <random>

#include "map_viewer.hpp"
#include "names.hpp"

#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr std::string_view kScenarioEnding = ".o1602";

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
                                   false});
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
                editor_notice(ui::tr("editor.undone", {what}), false, "undo");
            }
            break;
        }
        case Kind::Redo: {
            const std::string what = ui::tr("editor.step." + std::string(m_editor->document.history().next_redo()));
            if (m_editor->document.redo()) {
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
