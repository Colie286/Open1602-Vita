// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_folder_dialog.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "app_settings.hpp"
#include "diagnostics.hpp"

#include "o1602/core/file.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace fs = std::filesystem;

namespace {

enum class Answer { Choose, Quit, NoBox };

/// `text` (UTF-8) broken into lines of at most `width` letters where it has blanks: SDL's message box
/// on X11 makes one line as wide as it is, Windows and macOS break lines themselves.
std::string wrap(std::string_view text, std::size_t width = 72) {
    std::string out;
    std::size_t line = 0;  // letters in the current line
    while (!text.empty()) {
        const std::size_t end = std::min(text.find_first_of(" \n"), text.size());
        const std::string_view word = text.substr(0, end);
        const auto letters = static_cast<std::size_t>(std::count_if(
            word.begin(), word.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        if (line > 0 && line + 1 + letters > width) {
            out += '\n';
            line = 0;
        } else if (line > 0) {
            out += ' ';
            ++line;
        }
        out += word;
        line += letters;
        if (end < text.size() && text[end] == '\n') {
            out += '\n';
            line = 0;
        }
        text.remove_prefix(std::min(end + 1, text.size()));
    }
    return out;
}

/// The message box before the folder dialog: what is wanted (after `problem`, if any).
Answer ask(const std::string& problem) {
    std::string text = problem.empty() ? tr_utf8("game_folder.none_found") : problem;
    text = wrap(text + "\n\n" + tr_utf8("game_folder.choose_prompt"));
    const std::string choose = tr_utf8("game_folder.choose");
    const std::string quit = tr_utf8("game_folder.quit");
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, choose.c_str()},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, quit.c_str()},
    };
    const SDL_MessageBoxData data{
        static_cast<SDL_MessageBoxFlags>((problem.empty() ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_WARNING) |
                                         SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT),
        nullptr,
        "Open1602",
        text.c_str(),
        static_cast<int>(std::size(buttons)),
        buttons,
        nullptr};
    int pressed = -1;
    if (!SDL_ShowMessageBox(&data, &pressed)) {
        return Answer::NoBox;
    }
    return pressed == 1 ? Answer::Choose : Answer::Quit;
}

/// What the folder dialog answered; its callback may come on another thread.
struct Pick {
    std::mutex mutex;
    bool done = false;
    bool failed = false;
    std::string folder;  ///< UTF-8; empty when the player cancelled
    std::string error;
};

void SDLCALL on_folder(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* pick = static_cast<Pick*>(userdata);
    const std::lock_guard lock(pick->mutex);
    if (filelist == nullptr) {
        pick->failed = true;
        pick->error = SDL_GetError();
    } else if (filelist[0] != nullptr) {
        pick->folder = filelist[0];
    }
    pick->done = true;
}

/// The system's folder dialog, starting in `start`; waits for it on the main thread, handling the
/// events some systems need meanwhile.
void choose_folder(Pick& pick, const fs::path& start) {
    const SDL_PropertiesID props = SDL_CreateProperties();
    const std::string title = tr_utf8("game_folder.dialog_title");
    const std::string location = start.empty() ? std::string() : core::path_to_utf8(start / "");  // inside it
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
    if (!location.empty()) {
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
    }
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, on_folder, &pick, props);
    for (;;) {
        {
            const std::lock_guard lock(pick.mutex);
            if (pick.done) {
                break;
            }
        }
        (void)SDL_WaitEventTimeout(nullptr, 20);
    }
    SDL_DestroyProperties(props);
}

/// Where the folder dialog starts: in the folder last tried, or above the one that is gone.
fs::path start_folder(const fs::path& tried) {
    std::error_code ec;
    for (fs::path folder = tried; !folder.empty(); folder = folder.parent_path()) {
        if (fs::is_directory(folder, ec)) {
            return folder;
        }
        if (folder == folder.parent_path()) {
            break;
        }
    }
    return {};
}

void save_game_folder(const fs::path& settings_file, const fs::path& folder) {
    std::string text;
    if (auto bytes = core::read_file(settings_file)) {
        text.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    } else if (std::error_code exists_ec; fs::exists(settings_file, exists_ec) || exists_ec) {
        // There but not readable (a virus scanner holds it): written now it would lose every other
        // setting. The folder is asked for again at the next start.
        std::fprintf(stderr, "settings: %s; the game folder is not remembered\n", bytes.error().message.c_str());
        return;
    }
    text = set_setting_line(text, "game_folder", core::path_to_utf8(folder));
    std::error_code ec;
    fs::create_directories(settings_file.parent_path(), ec);
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    if (auto status = core::write_file_atomic(settings_file, std::span<const std::uint8_t>(data, text.size()));
        !status) {
        std::fprintf(stderr, "settings: %s\n", status.error().message.c_str());
    }
}

}  // namespace

std::string startup_language(std::string_view command_line, std::string_view settings) {
    if (!command_line.empty()) {
        return std::string(command_line);
    }
    if (!settings.empty() && settings != "auto") {
        return std::string(settings);
    }
    const std::vector<std::string> known = ui::builtin_languages();
    std::string code(ui::kFallbackLanguage);
    int count = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
        for (int i = 0; i < count; ++i) {
            const std::string language = locales[i]->language;
            if (std::find(known.begin(), known.end(), language) != known.end()) {
                code = language;
                break;
            }
        }
        SDL_free(reinterpret_cast<void*>(locales));
    }
    return code;
}

std::optional<assets::GameDirectory> ask_for_game_folder(const GameFolderSearch& search,
                                                         const fs::path& settings_file) {
    const auto give_up = [&](const std::string& why) {
        report_fatal(search.error.message + " (" + why + ")\n(set ANNO1602_DIR or pass --game-dir)");
        return std::nullopt;
    };
    // Events for the folder dialog; the game's own start of SDL comes later and counts on its own.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return give_up(std::string("no folder dialog without video: ") + SDL_GetError());
    }
    struct Quit {
        ~Quit() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
    } quit_video;

    std::string problem = search.saved_invalid.empty()
                              ? std::string()
                              : tr_utf8("game_folder.saved_invalid", {folder_problem(search.saved_invalid)});
    fs::path tried = search.saved_invalid;
    for (;;) {
        switch (ask(problem)) {
            case Answer::NoBox:
                return give_up(std::string("no message box: ") + SDL_GetError());
            case Answer::Quit:
                std::fputs("no game folder chosen\n", stderr);
                return std::nullopt;
            case Answer::Choose:
                break;
        }
        Pick pick;
        choose_folder(pick, start_folder(tried));
        if (pick.failed) {
            return give_up("no folder dialog: " + pick.error);
        }
        if (pick.folder.empty()) {
            problem.clear();  // cancelled: the question again, the player may quit there
            continue;
        }
        const fs::path folder = core::path_from_utf8(pick.folder);
        if (auto game = open_chosen_folder(folder)) {
            std::fprintf(stderr, "game folder chosen: %s\n", core::path_to_utf8(game->root()).c_str());
            save_game_folder(settings_file, game->root());
            return std::move(game).value();
        }
        std::fprintf(stderr, "game folder chosen: %s is no installation\n", pick.folder.c_str());
        problem = folder_problem(folder);
        tried = folder;
    }
}

}  // namespace o1602::app
