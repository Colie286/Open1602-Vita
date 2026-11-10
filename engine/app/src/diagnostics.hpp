// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the program's messages go. Linux and macOS: stderr, as always. Windows starts open1602.exe
// as a GUI program without a console, so stderr leads nowhere: a console it was started from gets
// the messages, else the file open1602.log in the user's data folder. Fatal errors also show in a
// message box, as a double click otherwise ends without a word (Linux and macOS: only when stderr
// is not a terminal).
#pragma once

#include <string_view>

namespace o1602::app {

/// Call first in main(), before anything is written to stderr.
void setup_diagnostics();

/// An error that ends the program: on stderr, and in a message box (UTF-8 text), on Linux and macOS
/// only when stderr is not a terminal; never after set_dialogs(false).
void report_fatal(std::string_view message);

/// false: report_fatal() shows no message box (--no-dialogs, a run from a script).
void set_dialogs(bool on);

}  // namespace o1602::app
