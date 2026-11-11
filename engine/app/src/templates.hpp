// SPDX-License-Identifier: GPL-3.0-or-later
//
// Templates (Open1602, as in later Anno games): pieces of a city the copy tool took, kept under a
// name to build again in any game. They live in the user's data folder, one text file each
// ("templates/<n>.txt"), and are the player's own: nothing of them goes into a savegame or to the
// other computers of a multiplayer game. A file holds the name (UTF-8), the piece's size and its
// items as the copy tool orders them (building Id, corner in the piece, rotation, footprint):
//
//     # Open1602 template
//     name = Weberviertel
//     size = 6 5
//     item = 20401 0 2 0 1 1
//
// Unknown lines are left out, so that later versions may add some.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "build_tool.hpp"
#include "template_window.hpp"

namespace o1602::app {

struct BuildTemplate {
    std::string name;  ///< Windows-1252, as the game's fonts show it
    BuildTool::Layout layout;
    std::string file;  ///< its file in the folder ("3.txt"), empty while it is not saved
};

/// The longest name a template takes, in characters.
inline constexpr std::size_t kMaxTemplateName = 24;
/// Items a template holds at most (the copy tool's drag takes no more).
inline constexpr std::size_t kMaxTemplateItems = 4096;

/// A template as the text of its file.
[[nodiscard]] std::string write_template(const BuildTemplate& t);
/// A template from the text of its file; nothing if it holds no item or no size.
[[nodiscard]] std::optional<BuildTemplate> read_template(std::string_view text);

/// The templates of a folder, by name.
class TemplateStore {
public:
    explicit TemplateStore(std::filesystem::path folder) : m_folder(std::move(folder)) {}

    /// Reads the folder again (another game may have saved some meanwhile).
    void load();
    [[nodiscard]] const std::vector<BuildTemplate>& all() const noexcept { return m_templates; }
    [[nodiscard]] const std::filesystem::path& folder() const noexcept { return m_folder; }

    /// Saves a new template; false if the file could not be written.
    bool add(std::string_view name, BuildTool::Layout layout);
    /// Gives the template at `index` another name; false if there is none or it could not be written.
    bool rename(std::size_t index, std::string name);
    /// Deletes the template at `index` and its file.
    bool remove(std::size_t index);
    /// A name for the next template: "Vorlage 3" if there are two.
    [[nodiscard]] std::string next_name() const;

private:
    bool write(const BuildTemplate& t) const;
    void sort();

    std::filesystem::path m_folder;
    std::vector<BuildTemplate> m_templates;
};

/// A template (or the piece in hand) as the window lists it: its size, its buildings (roads, walls,
/// fields and trees not counted), what building all of it costs, and its view from above. Items the
/// table does not know are left out.
[[nodiscard]] TemplateEntry template_entry(std::string name, const BuildTool::Layout& layout,
                                           const assets::BuildingTable& buildings);

/// A typed name as a template takes it: control characters left out, cut to kMaxTemplateName, spaces
/// at the ends dropped.
[[nodiscard]] std::string clean_template_name(std::string_view name);

}  // namespace o1602::app
