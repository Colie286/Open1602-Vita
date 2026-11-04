// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's markers and script (ADR 0013): named points and areas of the map that the
// scenario's script finds by their names (game.marker), and the script itself, loaded from a file
// of the user's folder "scripts". Each change is a step of undo.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "map_viewer.hpp"

#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/script/sandbox.hpp"
#include "o1602/sim/scenario_data.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// The folder of the scripts the editor loads, in the data folder.
constexpr std::string_view kScriptsFolder = "scripts";

std::string number(std::int64_t n) {
    return std::to_string(n);
}

/// A name for a new marker that no other has: marker_1, marker_2, ...
std::string free_marker_name(const std::vector<sim::ScenarioMarker>& markers) {
    for (std::size_t n = 1;; ++n) {
        std::string name = "marker_" + std::to_string(n);
        if (std::none_of(markers.begin(), markers.end(),
                         [&](const sim::ScenarioMarker& m) { return m.name == name; })) {
            return name;
        }
    }
}

/// The marker that holds the world tile, the smallest first (a point in an area is found).
std::optional<std::size_t> marker_at(const std::vector<sim::ScenarioMarker>& markers, std::int64_t x, std::int64_t y) {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < markers.size(); ++i) {
        const sim::ScenarioMarker& m = markers[i];
        if (x >= m.x && y >= m.y && x < std::int64_t{m.x} + m.width && y < std::int64_t{m.y} + m.height &&
            (!best ||
             std::uint64_t{m.width} * m.height < std::uint64_t{markers[*best].width} * markers[*best].height)) {
            best = i;
        }
    }
    return best;
}

/// The first problem of a script's text: its syntax error with the line, or nothing.
std::string script_problem(const std::string& source) {
    if (source.empty()) {
        return {};
    }
    script::Sandbox sandbox;
    if (const core::Status read = sandbox.compile("scenario", source); !read) {
        return read.error().message;
    }
    return {};
}

}  // namespace

void MapViewer::editor_markers_model(EditorModel& m) const {
    if (!m_editor) {
        return;
    }
    const sim::World& world = m_editor->document.world();
    for (const sim::ScenarioMarker& marker : world.markers()) {
        const std::string place = marker.shape == sim::ScenarioMarker::Shape::Point
                                      ? ui::tr("editor.markers.point", {number(marker.x), number(marker.y)})
                                      : ui::tr("editor.markers.area", {number(marker.x), number(marker.y),
                                                                       number(marker.width), number(marker.height)});
        m.markers.push_back(EditorMarkerRow{core::utf8_to_cp1252(marker.name), place});
    }
    if (m_editor->marker && *m_editor->marker < world.markers().size()) {
        m.marker = m_editor->marker;
    }
    const std::string& source = world.scenario_script();
    m.script_bytes = source.size();
    m.script_lines = source.empty() ? 0
                                    : static_cast<std::size_t>(std::count(source.begin(), source.end(), '\n')) +
                                          (source.back() == '\n' ? 0 : 1);
    if (!m_editor->script_file.empty()) {
        m.script_file = core::utf8_to_cp1252(core::path_to_utf8(m_editor->script_file.filename()));
        std::error_code ec;
        m.script_reload = std::filesystem::is_regular_file(m_editor->script_file, ec);
    }
    // Only while the dialog is open: the text is read through each frame then.
    if (m_hud && m_hud->editor_panel() != nullptr && m_hud->editor_panel()->page() == EditorPanel::Page::Script) {
        m.script_problem = core::utf8_to_cp1252(script_problem(source));
    }
}

void MapViewer::editor_markers_stepped(const std::vector<sim::ScenarioMarker>& before) {
    const std::vector<sim::ScenarioMarker>& after = m_editor->document.world().markers();
    const auto named = [](const std::vector<sim::ScenarioMarker>& list, const std::string& name) {
        return std::find_if(list.begin(), list.end(), [&](const sim::ScenarioMarker& m) { return m.name == name; });
    };
    // A marker back (a removal undone, a placing done again): that one chosen.
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (named(before, after[i].name) == before.end() && after.size() >= before.size()) {
            m_editor->marker = i;
            return;
        }
    }
    // Else the one chosen before, where it is now (or none, where it is gone).
    if (m_editor->marker && *m_editor->marker < before.size()) {
        const auto it = named(after, before[*m_editor->marker].name);
        if (it == after.end()) {
            m_editor->marker.reset();
        } else {
            m_editor->marker = static_cast<std::size_t>(it - after.begin());
        }
    }
}

void MapViewer::editor_markers_request(const EditorRequest& request) {
    using Kind = EditorRequest::Kind;
    const sim::World& world = m_editor->document.world();
    const std::vector<sim::ScenarioMarker>& markers = world.markers();
    switch (request.kind) {
        case Kind::MarkerSelect:
            if (request.index < markers.size()) {
                const sim::ScenarioMarker& mk = markers[request.index];
                m_editor->marker = request.index;
                m_editor->notice.clear();  // a note of another marker ("placed: marker_3") goes
                m_camera.center_on(render::TilePos{mk.x + mk.width / 2, mk.y + mk.height / 2});
            }
            break;
        case Kind::MarkerRename: {
            const std::string name = core::cp1252_to_utf8(request.name);
            if (request.index >= markers.size() || markers[request.index].name == name) {
                break;
            }
            if (!sim::valid_marker_name(name)) {
                editor_notice(ui::tr("editor.markers.name_rule"), true, "a marker's name refused");
                break;
            }
            if (std::any_of(markers.begin(), markers.end(),
                            [&](const sim::ScenarioMarker& m) { return m.name == name; })) {
                editor_notice(ui::tr("editor.markers.name_taken", {request.name}), true, "a marker's name taken");
                break;
            }
            std::vector<sim::ScenarioMarker> after = markers;
            after[request.index].name = name;
            m_editor->document.perform(std::make_unique<editor::MarkersEdit>(markers, std::move(after)));
            editor_notice(ui::tr("editor.markers.renamed", {request.name}), false, "marker renamed");
            break;
        }
        case Kind::MarkerRemove: {
            if (request.index >= markers.size()) {
                break;
            }
            const std::string name = core::utf8_to_cp1252(markers[request.index].name);
            std::vector<sim::ScenarioMarker> after = markers;
            after.erase(after.begin() + static_cast<std::ptrdiff_t>(request.index));
            m_editor->document.perform(std::make_unique<editor::MarkersEdit>(markers, std::move(after)));
            m_editor->marker.reset();
            editor_notice(ui::tr("editor.markers.removed", {name}), false, "marker removed");
            break;
        }
        case Kind::ListScripts: {
            m_editor_files.clear();
            m_editor_file_rows.clear();
            const std::filesystem::path folder = data_folder() / kScriptsFolder;
            std::error_code ec;
            std::filesystem::create_directories(folder, ec);  // so that the player finds where they go
            std::vector<std::filesystem::path> found;
            for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
                std::string ending = core::path_to_utf8(it->path().extension());
                std::transform(ending.begin(), ending.end(), ending.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::error_code file_ec;
                if (ending == ".lua" && it->is_regular_file(file_ec)) {
                    found.push_back(it->path());
                }
            }
            std::sort(found.begin(), found.end());
            const std::string group = ui::tr("editor.script.own_files");
            for (const std::filesystem::path& p : found) {
                m_editor_files.push_back(p);
                m_editor_file_rows.push_back(EditorFile{core::utf8_to_cp1252(core::path_to_utf8(p.filename())), group});
            }
            break;
        }
        case Kind::ScriptLoad:
            if (request.index < m_editor_files.size()) {
                (void)editor_load_script(m_editor_files[request.index]);
            }
            break;
        case Kind::ScriptReload:
            if (!m_editor->script_file.empty()) {
                (void)editor_load_script(m_editor->script_file);
            }
            break;
        case Kind::ScriptRemove:
            if (!world.scenario_script().empty()) {
                m_editor->document.perform(std::make_unique<editor::ScriptEdit>(world.scenario_script(), ""));
                editor_notice(ui::tr("editor.script.removed"), false, "script removed");
            }
            break;
        default:
            break;
    }
}

namespace {

/// The editor's state (section EDIT) is lines "key=value" of UTF-8; "script" names the file the
/// script was loaded from.
constexpr std::string_view kScriptKey = "script=";

}  // namespace

void MapViewer::editor_keep_script_file() {
    const std::vector<std::uint8_t>& state = m_state->world.editor_state();
    std::string text(state.begin(), state.end());
    std::string kept;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        const std::string_view line(text.data() + at, end - at);
        if (!line.starts_with(kScriptKey) && !line.empty()) {
            kept.append(line);
            kept.push_back('\n');
        }
        at = end + 1;
    }
    if (!m_editor->script_file.empty() && !m_state->world.scenario_script().empty()) {
        kept += std::string(kScriptKey) + core::path_to_utf8(m_editor->script_file) + "\n";
    }
    m_state->world.set_editor_state(std::vector<std::uint8_t>(kept.begin(), kept.end()));
}

void MapViewer::editor_find_script_file() {
    const std::vector<std::uint8_t>& state = m_state->world.editor_state();
    const std::string text(state.begin(), state.end());
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        const std::string_view line(text.data() + at, end - at);
        if (line.starts_with(kScriptKey)) {
            m_editor->script_file = core::path_from_utf8(std::string(line.substr(kScriptKey.size())));
        }
        at = end + 1;
    }
}

bool MapViewer::editor_load_script(const std::filesystem::path& file) {
    const std::string shown = core::utf8_to_cp1252(core::path_to_utf8(file.filename()));
    auto bytes = core::read_file(file);
    if (!bytes) {
        editor_notice(ui::tr("editor.script.failed", {shown}), true, bytes.error().message);
        return false;
    }
    if (bytes->size() > sim::kMaxScenarioScriptLength) {
        editor_notice(ui::tr("editor.script.too_large", {shown}), true, "a script larger than 1 MiB");
        return false;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    std::string source(core::skip_utf8_bom(text));
    if (!core::is_utf8(source)) {
        editor_notice(ui::tr("editor.script.not_utf8", {shown}), true, "a script not in UTF-8");
        return false;
    }
    m_editor->script_file = file;
    const std::string& before = m_editor->document.world().scenario_script();
    if (before == source) {
        editor_notice(ui::tr("editor.script.unchanged", {shown}), false, "script unchanged");
        return true;
    }
    m_editor->document.perform(std::make_unique<editor::ScriptEdit>(before, std::move(source)));
    const std::string problem = script_problem(m_editor->document.world().scenario_script());
    if (problem.empty()) {
        editor_notice(ui::tr("editor.script.loaded", {shown}), false, "script loaded");
    } else {
        editor_notice(ui::tr("editor.script.problem", {core::utf8_to_cp1252(problem)}), true,
                      "script loaded with an error: " + problem);
    }
    return true;
}

bool MapViewer::editor_marker_stroke(std::optional<render::TilePos> tile, Stroke phase) {
    if (!m_editor) {
        return false;
    }
    const sim::World& world = m_editor->document.world();
    switch (phase) {
        case Stroke::Begin:
            if (!tile) {
                return true;
            }
            m_editor->marker_from = {tile->x, tile->y};
            m_editor->marker_to = m_editor->marker_from;
            return true;
        case Stroke::Continue:
            if (tile && m_editor->marker_from) {
                m_editor->marker_to = {tile->x, tile->y};
            }
            return m_editor->marker_from.has_value();
        case Stroke::End: {
            if (!m_editor->marker_from || !m_editor->marker_to) {
                return false;
            }
            const auto [ax, ay] = *m_editor->marker_from;
            const auto [bx, by] = *m_editor->marker_to;
            m_editor->marker_from.reset();
            m_editor->marker_to.reset();
            const std::vector<sim::ScenarioMarker>& markers = world.markers();
            if (ax == bx && ay == by) {
                // A click: the marker there is selected; on open map a point is set.
                if (const auto found = marker_at(markers, ax, ay)) {
                    m_editor->marker = found;
                    return true;
                }
            }
            if (markers.size() >= sim::ScenarioMarker::kMaxMarkers) {
                editor_notice(ui::tr("editor.markers.too_many"), true, "too many markers");
                return true;
            }
            const std::int64_t x0 = std::clamp<std::int64_t>(std::min(ax, bx), 0, world.width() - 1);
            const std::int64_t y0 = std::clamp<std::int64_t>(std::min(ay, by), 0, world.height() - 1);
            const std::int64_t x1 = std::clamp<std::int64_t>(std::max(ax, bx), 0, world.width() - 1);
            const std::int64_t y1 = std::clamp<std::int64_t>(std::max(ay, by), 0, world.height() - 1);
            sim::ScenarioMarker m;
            m.name = free_marker_name(markers);
            m.shape = x0 == x1 && y0 == y1 ? sim::ScenarioMarker::Shape::Point : sim::ScenarioMarker::Shape::Area;
            m.x = static_cast<std::uint32_t>(x0);
            m.y = static_cast<std::uint32_t>(y0);
            m.width = static_cast<std::uint32_t>(x1 - x0 + 1);
            m.height = static_cast<std::uint32_t>(y1 - y0 + 1);
            std::vector<sim::ScenarioMarker> after = markers;
            after.push_back(m);
            m_editor->document.perform(std::make_unique<editor::MarkersEdit>(markers, std::move(after)));
            m_editor->marker = world.markers().size() - 1;
            editor_notice(ui::tr("editor.markers.added", {m.name}), false, "marker added");
            return true;
        }
    }
    return false;
}

}  // namespace o1602::app
