// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screen "Mods" of the main menu, Open1602's own: the packages of the mods folder, each turned
// on or off for new games, and the packages received from multiplayer hosts, which can be taken
// into the mods folder (docs/modding/packages.md). The screen only shows a ModsModel and answers
// with events; the viewer turns packages off, loads them anew and copies them.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "o1602/assets/game_content.hpp"
#include "o1602/ui/form.hpp"
#include "o1602/ui/list_scroll.hpp"

namespace o1602::app {

struct ModsModel {
    struct Entry {
        enum class State : std::uint8_t {
            On,        ///< in the mods folder, used by new games
            Off,       ///< in the mods folder, turned off
            Broken,    ///< in the mods folder, but cannot be loaded (`problem` says why)
            Received,  ///< received from a host: used only by the games that need it
        };
        std::string id;  ///< UTF-8, the folder's name
        std::filesystem::path folder;
        /// Name, version and description for the fonts (Windows-1252).
        std::string name;
        std::string version;
        std::string description;
        State state = State::On;
        /// The box is ticked: the player wants the package (not turned off), even if it cannot be
        /// loaded.
        bool wanted = true;
        /// (Broken) Why, short for the list and as a sentence for the chosen entry (Windows-1252).
        std::string short_problem;
        std::string problem;
        /// (Received) The mods folder has no package of its id yet, so it can be taken in.
        bool adoptable = false;
    };
    /// The mods folder's packages by id, then those received.
    std::vector<Entry> entries;
    /// What the last action did (a package taken in, or why it could not be), Windows-1252.
    std::string notice;
};

class ModsMenu {
public:
    struct Event {
        enum class Kind : std::uint8_t {
            Toggle,  ///< turns entries[index] on or off
            Adopt,   ///< copies the received entries[index] into the mods folder
            Close,   ///< back to the main menu
        };
        Kind kind = Kind::Close;
        std::size_t index = 0;
    };
    /// Rows the list shows at once.
    static constexpr std::size_t kRows = 11;

    explicit ModsMenu(ui::FormStyle style = {}) : m_form(style) {}

    void set_style(const ui::FormStyle& style) { m_form.set_style(style); }
    void set_scale(double scale) { m_form.set_scale(scale); }
    /// From the top of the list, nothing chosen.
    void open() noexcept {
        m_first = 0;
        m_selected.reset();
    }
    /// The entry chosen (whose description shows), if any.
    [[nodiscard]] std::optional<std::size_t> selected() const noexcept { return m_selected; }

    /// Everything to draw, relative to the 1024 x 768 design's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const ModsModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    std::optional<Event> mouse(const ModsModel& model, std::int32_t x, std::int32_t y, bool down,
                               ui::MouseButton button = ui::MouseButton::Left);
    /// The mouse wheel (`notches` up positive): the list a row per notch.
    void wheel(const ModsModel& model, std::int32_t notches) noexcept {
        m_first = ui::wheel_scroll(m_first, model.entries.size(), kRows, notches);
    }
    /// The text of an entry's state ("an", "aus", ...).
    [[nodiscard]] static std::string state_text(ModsModel::Entry::State state);
    /// Why a package is not loaded, short for the list ("Braucht harbour") and as a sentence
    /// ("Braucht harbour >=1.0, das nicht installiert ist."), Windows-1252.
    [[nodiscard]] static std::string short_problem_text(const assets::PackageProblem& problem);
    [[nodiscard]] static std::string problem_text(const assets::PackageProblem& problem);

private:
    void layout(const ModsModel& model);

    ui::Form m_form;
    std::size_t m_first = 0;
    std::optional<std::size_t> m_selected;
};

}  // namespace o1602::app
