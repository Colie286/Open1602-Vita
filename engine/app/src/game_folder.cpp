// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_folder.hpp"

#include <cstdio>
#include <system_error>
#include <utility>
#include <vector>

#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace fs = std::filesystem;

using assets::GameDirectory;

namespace {

fs::path typed_path(std::string_view text) {
    return core::path_from_utf8(GameDirectory::clean_path_text(text));
}

GameFolderSearch named(core::Result<GameDirectory> dir) {
    GameFolderSearch out;
    if (dir) {
        out.game = std::move(dir).value();
    } else {
        out.error = dir.error();
    }
    return out;
}

}  // namespace

GameFolderSearch find_game_folder(std::string_view command_line, std::string_view saved,
                                  const std::optional<std::string>& environment) {
    if (const fs::path folder = typed_path(command_line); !folder.empty()) {
        return named(GameDirectory::open(folder));
    }
    if (environment) {
        if (const fs::path folder = typed_path(*environment); !folder.empty()) {
            return named(GameDirectory::open(folder));
        }
    }
    GameFolderSearch out;
    if (const fs::path folder = typed_path(saved); !folder.empty()) {
        auto dir = GameDirectory::open(folder);
        if (dir) {
            out.game = std::move(dir).value();
            return out;
        }
        // Moved or uninstalled: an installation elsewhere still counts, else the player is asked.
        std::fprintf(stderr, "game folder from the settings: %s\n", dir.error().message.c_str());
        out.saved_invalid = folder;
    }
    for (const fs::path& candidate : GameDirectory::installation_candidates()) {
        if (auto dir = GameDirectory::open(candidate)) {
            out.game = std::move(dir).value();
            return out;
        }
    }
    out.error = core::make_error(core::ErrorCode::NotFound, "no compatible original game installation found in the usual folders");
    out.ask = true;
    return out;
}

core::Result<GameDirectory> open_chosen_folder(const fs::path& folder) {
    auto dir = GameDirectory::open(folder);
    if (dir) {
        return dir;
    }
    std::vector<GameDirectory> inside;
    for (const fs::path& child : GameDirectory::folders_named_1602(folder)) {
        if (auto found = GameDirectory::open(child)) {
            inside.push_back(std::move(found).value());
        }
    }
    if (inside.size() == 1) {
        return inside.front();
    }
    return dir;  // none or several: the player chooses again
}

std::string folder_problem(const fs::path& folder) {
    const std::string name = core::path_to_utf8(folder);
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return tr_utf8("game_folder.missing", {name});
    }
    if (const auto file = GameDirectory::missing_file(folder)) {
        std::string inside;
        std::size_t count = 0;
        for (const fs::path& child : GameDirectory::folders_named_1602(folder)) {
            if (!GameDirectory::missing_file(child)) {
                inside += "\n" + core::path_to_utf8(child.filename());
                ++count;
            }
        }
        if (count > 1) {
            return tr_utf8("game_folder.several", {name, inside});
        }
        return tr_utf8("game_folder.not_installation", {name, *file});
    }
    return {};
}

std::string tr_utf8(std::string_view key, std::initializer_list<std::string_view> args) {
    // The arguments go in after the conversion: Windows-1252 cannot hold every letter of a path.
    std::string text = core::cp1252_to_utf8(ui::tr(key));
    std::size_t n = 1;
    for (const std::string_view arg : args) {
        const std::string mark = "{" + std::to_string(n++) + "}";
        for (std::size_t at = text.find(mark); at != std::string::npos; at = text.find(mark, at + arg.size())) {
            text.replace(at, mark.size(), arg);
        }
    }
    return text;
}

}  // namespace o1602::app
