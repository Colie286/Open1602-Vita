// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor and the content packages (ADR 0013, docs/editor.md): the packages a scenario needs
// with their versions, and a scenario saved into a package as one of its own: the map into the
// package's folder maps, its table into the package's scenarios.toml (docs/modding/scenarios.md),
// the file's comments, order and other keys kept.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "main_menu.hpp"
#include "map_viewer.hpp"

#include "o1602/assets/package_scenarios.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/scenario_data.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// The folder of a package's maps and the ending of a map the editor writes there.
constexpr std::string_view kMapsFolder = "maps";
constexpr std::string_view kMapEnding = ".o1602";

/// A name a package's scenario may have, made of a document's name (Windows-1252): lower case,
/// every other character '_'; empty if nothing is left.
std::string scenario_name_of(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 && u < 0x80) {
            out.push_back(static_cast<char>(std::tolower(u)));
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.size() > sim::ScenarioMarker::kMaxNameLength) {
        out.resize(sim::ScenarioMarker::kMaxNameLength);
    }
    return out;
}

/// Whether `code` can be the language of a key `name.<language>`: lower case letters and '-'.
bool language_code(std::string_view code) noexcept {
    return !code.empty() && code.size() <= 16 &&
           std::all_of(code.begin(), code.end(), [](const char c) { return (c >= 'a' && c <= 'z') || c == '-'; });
}

/// The lines of the scenario's table in scenarios.toml that the editor writes: the map, the stars,
/// and the name and briefing in each language of the description; and the keys of the languages
/// it has no text in (German and English), taken out.
std::pair<std::vector<core::DataLine>, std::vector<std::string>> scenario_lines(const sim::World& world,
                                                                                const std::string& map) {
    std::vector<core::DataLine> lines{{"file", core::data_string(map)}};
    std::vector<std::string> removed;
    const auto& info = world.scenario_info();
    if (info) {
        lines.push_back({"stars", std::to_string(info->stars)});
    }
    const auto texts = [&](const char* key, const std::vector<sim::ScenarioText>* given) {
        std::vector<std::string> languages = {"de", "en"};
        if (given != nullptr) {
            for (const sim::ScenarioText& t : *given) {
                if (!t.text.empty() && language_code(t.language)) {
                    lines.push_back({std::string(key) + "." + t.language, core::data_string(t.text)});
                    languages.erase(std::remove(languages.begin(), languages.end(), t.language), languages.end());
                }
            }
        }
        for (const std::string& l : languages) {
            removed.push_back(std::string(key) + "." + l);
        }
    };
    texts("name", info ? &info->names : nullptr);
    texts("briefing", info ? &info->briefings : nullptr);
    return {std::move(lines), std::move(removed)};
}

}  // namespace

void MapViewer::package_written(assets::ModPackage& package) {
    // The package's files changed: its hash, and its scenarios and campaigns for the menu's list.
    package.hash = assets::package_hash(package.folder);
    load_package_scenarios();
    if (m_hud) {
        if (MainMenu* menu = m_hud->menu()) {
            menu->set_package_scenarios(m_package_scenarios, m_package_campaigns);
        }
    }
}

std::optional<std::pair<std::string, std::string>> MapViewer::editor_package_of(
    const std::filesystem::path& file) const {
    if (file.empty() || core::path_to_utf8(file.extension()) != kMapEnding) {
        return std::nullopt;
    }
    std::error_code ec;
    const std::filesystem::path maps = std::filesystem::weakly_canonical(file.parent_path(), ec);
    if (ec || core::path_to_utf8(maps.filename()) != kMapsFolder) {
        return std::nullopt;
    }
    for (const assets::ModPackage& p : m_packages) {
        if (!p.archive.empty()) {
            continue;
        }
        const std::filesystem::path folder = std::filesystem::weakly_canonical(p.folder, ec);
        const std::string name = core::path_to_utf8(file.stem());
        if (!ec && maps.parent_path() == folder && sim::valid_marker_name(name)) {
            return std::pair{p.id, name};
        }
    }
    return std::nullopt;
}

void MapViewer::editor_packages_model(EditorModel& m) const {
    const sim::World& world = m_editor->document.world();
    if (const auto& needed = world.packages()) {
        for (const assets::ContentPackage& p : *needed) {
            const auto it = std::find_if(m_packages.begin(), m_packages.end(),
                                         [&](const assets::ModPackage& q) { return q.id == p.id; });
            std::string state;
            if (it == m_packages.end()) {
                state = ui::tr("editor.packages.missing");
            } else if (it->version != p.version) {
                state = ui::tr("editor.packages.other_version",
                               {core::utf8_to_cp1252(it->version.empty() ? "-" : it->version)});
            } else {
                state = ui::tr("editor.packages.installed");
            }
            m.packages.push_back(
                EditorRule{core::utf8_to_cp1252(assets::package_label(p)), state, it != m_packages.end()});
        }
    }
    for (const assets::ModPackage& p : m_packages) {
        if (p.archive.empty()) {
            if (p.id == m_editor->package) {
                m.package_folder = m.package_folders.size();
            }
            m.package_folders.push_back(p.id);
        }
    }
    for (const assets::PackageScenario& s : m_package_scenarios) {
        m.package_scenarios.push_back(s.id);
    }
    if (!m_editor->package.empty()) {
        m.package_scenario = m_editor->package + ":" + m_editor->package_scenario;
        // Listed only once the scenario is one of the package's (saved there and read).
        if (std::none_of(m_package_scenarios.begin(), m_package_scenarios.end(),
                         [&](const assets::PackageScenario& s) { return s.id == m.package_scenario; })) {
            m.package_scenario.clear();
        }
        const std::string prefix = m_editor->package + ":";
        for (const assets::PackageCampaign& c : m_package_campaigns) {
            if (c.package == m_editor->package) {
                m.campaigns.push_back(EditorModel::Campaign{c.id.substr(prefix.size()), c.parts});
            }
        }
    }
    // The name offered: the scenario's in its package, else the file's, else the description's.
    m.package_name = m_editor->package_scenario;
    if (m.package_name.empty()) {
        m.package_name =
            scenario_name_of(m_editor->file.empty() ? m_editor->source_name
                                                    : core::utf8_to_cp1252(core::path_to_utf8(m_editor->file.stem())));
    }
    if (const auto& info = world.scenario_info(); m.package_name.empty() && info) {
        for (const sim::ScenarioText& t : info->names) {
            if (m.package_name.empty()) {
                m.package_name = scenario_name_of(core::utf8_to_cp1252(t.text));
            }
        }
    }
}

void MapViewer::editor_save_to_package(const std::string& package, const std::string& name) {
    if (!m_editor) {
        return;
    }
    const auto failed = [&](const std::string& why) {
        editor_notice(ui::tr("editor.package_save.failed", {core::utf8_to_cp1252(why)}), true,
                      "saving into package " + package + " failed: " + why);
    };
    const auto it = std::find_if(m_packages.begin(), m_packages.end(),
                                 [&](const assets::ModPackage& p) { return p.id == package && p.archive.empty(); });
    if (it == m_packages.end()) {
        failed("the package is not installed as a folder");
        return;
    }
    if (!sim::valid_marker_name(name)) {
        failed("the name is not one of lower case letters, digits and _");
        return;
    }
    // The table first: a scenarios.toml the editor cannot read stays as it is, and so does the map.
    const std::filesystem::path toml = it->folder / std::string(assets::kScenariosFile);
    std::string text;
    std::error_code ec;
    if (std::filesystem::exists(toml, ec)) {
        auto bytes = core::read_file(toml);
        if (!bytes) {
            failed(bytes.error().message);
            return;
        }
        text.assign(bytes->begin(), bytes->end());
    }
    const std::string map = std::string(kMapsFolder) + "/" + name + std::string(kMapEnding);
    const auto [lines, removed] = scenario_lines(m_editor->document.world(), map);
    auto updated = core::set_data_table(text, name, lines, removed);
    if (!updated) {
        failed(std::string(assets::kScenariosFile) + ": " + updated.error().message);
        return;
    }
    if (!editor_save(it->folder / core::path_from_utf8(map))) {
        return;  // the notice says why
    }
    const std::vector<std::uint8_t> bytes(updated->begin(), updated->end());
    if (auto written = core::write_file_atomic(toml, bytes); !written) {
        failed(std::string(assets::kScenariosFile) + ": " + written.error().message);
        return;
    }
    m_editor->package = package;
    m_editor->package_scenario = name;
    package_written(*it);
    const std::string id = package + ":" + name;
    editor_notice(ui::tr("editor.package_save.saved", {core::utf8_to_cp1252(id)}), false,
                  "saved as the package's scenario " + id);
}

void MapViewer::editor_campaign(const std::string& campaign, bool add) {
    if (!m_editor || m_editor->package.empty()) {
        return;
    }
    const std::string& package = m_editor->package;
    const std::string id = package + ":" + m_editor->package_scenario;
    const auto failed = [&](const std::string& why) {
        editor_notice(ui::tr("editor.campaign.failed", {core::utf8_to_cp1252(why)}), true,
                      "campaign " + package + ":" + campaign + " not changed: " + why);
    };
    const auto it = std::find_if(m_packages.begin(), m_packages.end(),
                                 [&](const assets::ModPackage& p) { return p.id == package && p.archive.empty(); });
    if (it == m_packages.end() || !sim::valid_marker_name(campaign)) {
        failed("no campaign of a package installed as a folder");
        return;
    }
    const auto known = std::find_if(m_package_campaigns.begin(), m_package_campaigns.end(),
                                    [&](const assets::PackageCampaign& c) { return c.id == package + ":" + campaign; });
    const std::vector<std::string> parts =
        known != m_package_campaigns.end() ? known->parts : std::vector<std::string>{};
    std::vector<core::DataLine> lines;
    std::vector<std::string> removed;
    std::size_t part = 0;  // from 1
    if (add) {
        if (std::find(parts.begin(), parts.end(), id) != parts.end() || parts.size() >= assets::kMaxCampaignParts) {
            failed("the scenario is a part already, or the campaign is full");
            return;
        }
        part = parts.size() + 1;
        lines.push_back({"part." + std::to_string(part), core::data_string(m_editor->package_scenario)});
        if (parts.empty()) {
            // A new campaign: its name in the list as the scenario's, until the file gives another.
            if (const auto& info = m_editor->document.world().scenario_info()) {
                // Told apart from the scenario of the same name in the list.
                for (const sim::ScenarioText& t : info->names) {
                    if (!t.text.empty() && (t.language == "de" || t.language == "en")) {
                        const std::string& word = t.language == "de" ? "Kampagne" : "campaign";
                        lines.push_back({"name." + t.language, core::data_string(t.text + " (" + word + ")")});
                    }
                }
            }
        }
    } else {
        if (parts.size() < 2 || parts.back() != id) {
            failed("only the last part, and not the first, is taken out");
            return;
        }
        part = parts.size();
        removed.push_back("part." + std::to_string(part));
    }
    const std::filesystem::path toml = it->folder / std::string(assets::kCampaignsFile);
    std::string text;
    std::error_code ec;
    if (std::filesystem::exists(toml, ec)) {
        auto bytes = core::read_file(toml);
        if (!bytes) {
            failed(bytes.error().message);
            return;
        }
        text.assign(bytes->begin(), bytes->end());
    }
    auto updated = core::set_data_table(text, campaign, lines, removed);
    if (!updated) {
        failed(std::string(assets::kCampaignsFile) + ": " + updated.error().message);
        return;
    }
    const std::vector<std::uint8_t> bytes(updated->begin(), updated->end());
    if (auto written = core::write_file_atomic(toml, bytes); !written) {
        failed(std::string(assets::kCampaignsFile) + ": " + written.error().message);
        return;
    }
    package_written(*it);
    const std::string name = core::utf8_to_cp1252(package + ":" + campaign);
    editor_notice(ui::tr(add ? "editor.campaign.added" : "editor.campaign.removed", {name, std::to_string(part)}),
                  false, std::string(add ? "added to" : "taken out of") + " campaign " + package + ":" + campaign);
}

}  // namespace o1602::app
