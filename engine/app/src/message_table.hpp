// SPDX-License-Identifier: GPL-3.0-or-later
//
// The messages of the status bar as data (ADR 0007, docs/modding/messages.md): what each one shows
// (its text and icon) and plays (voice or own sound, video). The original's messages come first,
// at their MELDUNG numbers ("anno1602:<n>"); content packages add their own ("mymod:lighthouse")
// and change single fields of existing ones, from a file `messages.toml` each.
//
// At runtime a message is its index (MessageId); the string ids are only for loading and for the
// scripts that will post them (M8). Messages are presentation only and never touch the
// simulation, so a mod's messages need not be the same on every computer of a multiplayer game.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "o1602/assets/game_content.hpp"
#include "o1602/audio/game_audio.hpp"
#include "o1602/core/data_file.hpp"

namespace o1602::app {

/// A message's index in its MessageTable; the original's messages keep their MELDUNG numbers.
using MessageId = std::uint32_t;

/// The icons of the status bar (LEIST.GAD, docs/re/ui.md "Message icons"). The first four are
/// drawn in the colour of the player the message is about.
enum class MessageIcon : std::uint8_t {
    Notice,  ///< the exclamation mark
    Scroll,  ///< trade and diplomacy
    Combat,  ///< the sword (the pirates' skull for them)
    Player,  ///< the mouth: the fellow players
    Plague,
    Fire,
    Drought,
    Volcano,
};

struct MessageDefinition {
    std::string id;  ///< "anno1602:42", "mymod:lighthouse"
    MessageIcon icon = MessageIcon::Notice;
    /// A package's icon it shows instead ("<package>:<name>", icons.toml), or empty.
    std::string picture;
    /// Without a text of its own: the original's text of this MELDUNG number (`like`).
    std::optional<std::uint16_t> meldung;
    /// The status bar's text by language code ("" for every language), Windows-1252, with {name}
    /// placeholders.
    std::map<std::string, std::string, std::less<>> texts;
    audio::MessagePlayback playback;
};

/// A placeholder's name and its value (Windows-1252): {"player", "Merlin"} fills {player}.
using MessageArgs = std::vector<std::pair<std::string_view, std::string_view>>;

class MessageTable {
public:
    /// The package of the original's messages.
    static constexpr std::string_view kOriginal = "anno1602";
    /// The file a package describes its messages in.
    static constexpr std::string_view kFileName = "messages.toml";

    /// The original's messages only.
    MessageTable();
    /// One table of the original's messages for whoever has none of their own (tests, menus).
    [[nodiscard]] static const MessageTable& original();

    [[nodiscard]] std::size_t size() const noexcept { return m_messages.size(); }
    [[nodiscard]] const MessageDefinition* get(MessageId id) const noexcept {
        return id < m_messages.size() ? &m_messages[id] : nullptr;
    }
    /// The message of a string id; "anno1602:0x2A" finds "anno1602:42" too.
    [[nodiscard]] std::optional<MessageId> find(std::string_view id) const;

    /// The text of a message in `language` (else the one for every language, else English) with its
    /// placeholders filled; empty if it has none (the status bar then shows the MELDUNG text).
    [[nodiscard]] std::string text(MessageId id, std::string_view language, const MessageArgs& args = {}) const;

    /// Takes a package's messages.toml: new messages under `package`, changes to existing ones
    /// (tables named with a package, ["anno1602:42"]). Sound files are found in `folder`. What is
    /// wrong in a single field is passed over with a warning ("mymod/messages.toml line 3: ...");
    /// a file that cannot be read at all is an error and changes nothing.
    core::Status load_package(std::string_view package, std::string_view utf8, const std::filesystem::path& folder,
                              std::vector<std::string>& warnings);
    /// The messages.toml of every package that has one (<package folder>/messages.toml), in the
    /// order given (their load order, assets::load_mod_packages).
    void load_mods(std::span<const assets::ModPackage> packages, std::vector<std::string>& warnings);

private:
    /// Applies the fields of `table` to `m`.
    void apply(const core::DataTable& table, MessageDefinition& m, const std::filesystem::path& folder,
               const std::string& where, std::vector<std::string>& warnings) const;

    std::vector<MessageDefinition> m_messages;
    std::unordered_map<std::string, MessageId> m_ids;
};

/// Fills {name} placeholders of `pattern` from `args`; an unknown one stays as it is.
[[nodiscard]] std::string fill_placeholders(std::string_view pattern, const MessageArgs& args);

}  // namespace o1602::app
