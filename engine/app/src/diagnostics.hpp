// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the program's messages go. Started from a terminal, or with stderr redirected: stderr, as
// always. Started from a desktop launcher, the Finder or by a double click, stderr leads nowhere
// (Windows starts open1602.exe as a GUI program without a console; Linux launchers pass the system
// log, macOS /dev/null): the messages go into open1602.log in the user's data folder, the log of
// the run before into open1602.previous.log. On Windows a console the game was started from gets
// them. Fatal errors also show in a message box, as a double click otherwise ends without a word
// (Linux and macOS: only when stderr is not a terminal). A crash (invalid memory access, an
// uncaught exception) writes what happened, and where, before the program ends.
#pragma once

#include <string_view>

namespace o1602::app {

/// Call first in main(), before anything is written to stderr.
void setup_diagnostics();

/// Linux and macOS: SIGTERM, SIGINT or SIGHUP arrived (and was logged); the main loop ends the
/// game as if the window was closed. Windows: always false (SDL reports those as SDL_EVENT_QUIT).
[[nodiscard]] bool termination_requested();

/// The last line of a log file: the program ended by itself. A log without it ended in a crash
/// or was killed.
void report_exit(int code);

/// An error that ends the program: on stderr, and in a message box (UTF-8 text), on Linux and macOS
/// only when stderr is not a terminal; never after set_dialogs(false).
void report_fatal(std::string_view message);

/// false: report_fatal() shows no message box (--no-dialogs, a run from a script).
void set_dialogs(bool on);

}  // namespace o1602::app
