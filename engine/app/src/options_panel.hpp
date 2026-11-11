// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings of the side panel (mode 30014): OPTION.GAD with its pages DISK (savegames, leaving the game), MUSIK
// (music and effects) and ANNO (voice messages and videos). The panel only shows and edits an
// OptionsModel; what has to happen beyond that comes back as OptionsEvents.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/gad.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/audio/game_audio.hpp"
#include "o1602/ui/panel.hpp"

namespace o1602::app {

/// What the settings show; the viewer fills it, the panel changes `audio` and `mission_window`.
struct OptionsModel {
    audio::AudioSettings audio;
    std::vector<std::string> tracks;  ///< music titles in the order of the list
    std::string playing;              ///< title playing now
    /// The savegames by slot (empty name: free), as many as there are; the list scrolls.
    std::vector<std::string> slot_names;
    std::vector<std::uint8_t> slot_humans;  ///< human players in the savegame (icon from 2)
    /// Per slot the player, time played and map size (save_details); empty if unknown. The status
    /// bar shows it over the slot.
    std::vector<std::string> slot_details;
    /// The first slots hold games Open1602 keeps by itself (lastgame): they load, but are never
    /// named or saved into from the list.
    std::size_t reserved_slots = 0;
    bool fullscreen = false;      ///< the window fills the screen
    bool mission_window = false;  ///< "Auftrag ansehen"
    bool multiplayer = false;
};

/// What the settings ask of the viewer.
struct OptionsEvent {
    enum class Kind : std::uint8_t {
        Save,           ///< into slot `value`, named `text`
        Load,           ///< slot `value`
        Quit,           ///< "Ende": leave the game
        Fullscreen,     ///< OptionsModel::fullscreen changed (`value` 1: on)
        Display,        ///< open the window "Anzeige"
        Audio,          ///< OptionsModel::audio changed (`value` 1: a volume slider is still held,
                        ///< so the change is heard but not yet saved; a final one follows)
        PlayTrack,      ///< play `text` now (and enable it)
        TestEffects,    ///< the test sound
        MissionWindow,  ///< OptionsModel::mission_window changed
    };
    Kind kind = Kind::Audio;
    std::size_t value = 0;
    std::string text{};
};

class OptionsPanel {
public:
    /// DISK gets the scroll arrows of MUSIK for its list of savegames.
    OptionsPanel(assets::GadgetLayout option, assets::GadgetLayout disk, assets::GadgetLayout musik,
                 assets::GadgetLayout anno, const assets::TextTable* texts);

    void set_scale(double scale);
    /// The page shown now.
    [[nodiscard]] ui::Panel& panel();
    /// Back to the first page when the mode is entered.
    void opened() noexcept;
    /// Escape: from a page back to the first; false on the first page.
    bool back();

    void refresh(const OptionsModel& model);
    /// The mouse in pixels relative to the side panel.
    std::vector<OptionsEvent> mouse(std::int32_t x, std::int32_t y, bool down, OptionsModel& model);
    [[nodiscard]] std::optional<std::string> help(const std::vector<std::string>& leiste, const OptionsModel& model);
    /// The mouse wheel over the panel (`notches` up positive): the savegames and the music titles
    /// scroll a row per notch (Open1602; the History Edition marks the savegame rows for it).
    /// False on a page without a list.
    bool wheel(std::int32_t notches, const OptionsModel& model);

    /// A savegame's name is being typed (clicking a slot starts it, mode 3).
    [[nodiscard]] bool editing() const noexcept { return m_editing; }
    /// Typed text (Windows-1252); Backspace, Enter and Escape as `key` 8, 13 and 27. Enter on a
    /// free slot saves the new game at once; Escape drops the name typed.
    void type(std::string_view text);
    [[nodiscard]] std::optional<OptionsEvent> key(int key);

private:
    enum class Page : std::uint8_t { Root, Disk, Musik, Anno };
    std::string text(std::string_view section, std::size_t line) const;
    void refresh_disk(const OptionsModel& model);
    void refresh_musik(const OptionsModel& model);
    void refresh_anno(const OptionsModel& model);
    void stop_editing();

    ui::Panel m_option;
    ui::Panel m_disk;
    ui::Panel m_musik;
    ui::Panel m_anno;
    const assets::TextTable* m_texts;
    Page m_page = Page::Root;
    std::size_t m_slot = 0;             ///< the selected savegame
    std::size_t m_slot_first = 0;       ///< first savegame shown
    std::size_t m_slot_rows = 0;        ///< savegame rows of the layout (12, the History Edition 10)
    std::size_t m_slot_rows_shown = 0;  ///< of them with savegames (the arrows may take the last)
    std::int32_t m_slots_down = 0;      ///< the arrows that scroll the savegames
    std::int32_t m_slots_up = 0;
    bool m_editing = false;
    bool m_new_slot = false;        ///< the name is typed into a free slot (Enter saves)
    std::string m_name;             ///< the name being typed
    bool m_renamed = false;         ///< m_name is the new name of the selected slot (until saved)
    std::size_t m_list_first = 0;   ///< first music title shown
    bool m_volume_unsaved = false;  ///< a volume slider moved and has not been let go yet
    std::int32_t m_humans_icon = 507;
};

}  // namespace o1602::app
