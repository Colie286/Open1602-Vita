// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnostics.hpp"
#ifdef O1602_EXPERIMENTAL_VITA
#include "o1602/platform/vita.hpp"
#endif

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

#include "o1602/core/file.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define O1602_HAVE_BACKTRACE 1
#endif
#endif

#ifndef O1602_VERSION
#define O1602_VERSION "unknown"
#endif

namespace o1602::app {

namespace {

bool g_dialogs = true;
bool g_log_file = false;                       ///< stderr goes into open1602.log
volatile std::sig_atomic_t g_termination = 0;  ///< SIGTERM, SIGINT or SIGHUP arrived

/// The first line of every log file: which version, and when (local time), so that a log can be told
/// apart from the next one and matched with what the player remembers.
void write_header() {
    char when[32] = "?";
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    const bool ok = localtime_s(&local, &now) == 0;
#else
    const bool ok = localtime_r(&now, &local) != nullptr;
#endif
    if (ok) {
        (void)std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
    }
    std::fprintf(stderr, "Open1602 %s, started %s\n", O1602_VERSION, when);
}

/// Sends stderr into open1602.log in the user's data folder (MapViewer::data_folder(), SDL makes
/// it). The log of the run before is kept as open1602.previous.log: after a crash the game is
/// usually started again at once, which would otherwise overwrite the one log that tells about it.
void log_to_file() {
    char* folder = SDL_GetPrefPath(nullptr, "Open1602");
    if (folder == nullptr) {
        return;
    }
    const std::filesystem::path dir = core::path_from_utf8(folder);
    SDL_free(folder);
    const std::filesystem::path log = dir / "open1602.log";
    std::error_code ec;
    if (std::filesystem::exists(log, ec)) {
        std::filesystem::rename(log, dir / "open1602.previous.log", ec);
    }
    FILE* stream = nullptr;
#if defined(_WIN32)
    if (_wfreopen_s(&stream, log.c_str(), L"w", stderr) != 0) {
        return;
    }
#else
    stream = std::freopen(log.c_str(), "w", stderr);
    if (stream == nullptr) {
        return;
    }
#endif
    std::setvbuf(stderr, nullptr, _IONBF, 0);  // nothing is lost when the program crashes
    g_log_file = true;
    write_header();
}

[[noreturn]] void on_terminate() {
    // An exception nothing caught on another thread, or a noexcept function that threw. main()
    // catches those of the main thread itself.
    std::fprintf(stderr, "fatal: std::terminate (an exception was not caught)\n");
    std::abort();  // the crash handler below adds where it happened
}

#if defined(_WIN32)

LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    // The address as module + offset: the same in every run, whatever address the module loaded at.
    HMODULE module = nullptr;
    wchar_t name[MAX_PATH] = L"?";
    std::uintptr_t offset = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(record->ExceptionAddress), &module) != 0) {
        offset -= reinterpret_cast<std::uintptr_t>(module);
        (void)GetModuleFileNameW(module, name, MAX_PATH);
    }
    std::fprintf(stderr, "fatal: exception 0x%08lX at %ls+0x%llX\n", static_cast<unsigned long>(record->ExceptionCode),
                 name, static_cast<unsigned long long>(offset));
    return EXCEPTION_CONTINUE_SEARCH;  // Windows' own error report follows
}

[[maybe_unused]] void install_crash_handler() {
    (void)SetUnhandledExceptionFilter(on_crash);
}

#elif !defined(O1602_EXPERIMENTAL_VITA)

constexpr int kCrashSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

void write_text(const char* text) {
    // Only async-signal-safe calls in the handler: write(), not fprintf().
    (void)!write(STDERR_FILENO, text, std::strlen(text));
}

const char* signal_name(int sig) {
    switch (sig) {
        case SIGSEGV:
            return "SIGSEGV (invalid memory access)";
        case SIGBUS:
            return "SIGBUS (invalid memory access)";
        case SIGILL:
            return "SIGILL (illegal instruction)";
        case SIGFPE:
            return "SIGFPE (arithmetic error)";
        case SIGABRT:
            return "SIGABRT (abort)";
        default:
            return "unknown signal";
    }
}

void on_crash(int sig) {
    write_text("fatal: ");
    write_text(signal_name(sig));
    write_text("\n");
#if defined(O1602_HAVE_BACKTRACE)
    void* frames[64];
    const int count = backtrace(frames, 64);
    write_text("backtrace:\n");
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
    // SA_RESETHAND restored the default action: the signal ends the program as before, with a core
    // dump where the system keeps one.
    (void)raise(sig);
}

[[maybe_unused]] void install_crash_handler() {
#if defined(O1602_HAVE_BACKTRACE)
    // The first call may load the unwinder, which must not happen inside the handler.
    void* frame = nullptr;
    (void)backtrace(&frame, 1);
#endif
    // A stack of its own, so that a stack overflow can still be reported.
    static char alt_stack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    (void)sigaltstack(&ss, nullptr);
    struct sigaction action = {};
    action.sa_handler = on_crash;
    sigemptyset(&action.sa_mask);
    action.sa_flags = static_cast<int>(SA_ONSTACK | SA_RESETHAND);
    for (const int sig : kCrashSignals) {
        (void)sigaction(sig, &action, nullptr);
    }
}

void on_termination(int sig) {
    g_termination = 1;
    write_text(sig == SIGTERM  ? "quit: SIGTERM (the system or another program ended the game)\n"
               : sig == SIGINT ? "quit: SIGINT (Ctrl+C in the terminal)\n"
                               : "quit: SIGHUP (the terminal or session went away)\n");
}

/// SDL would turn SIGTERM and SIGINT into a silent SDL_EVENT_QUIT, and SIGHUP ends the program
/// without a word: here all three are logged and end the game through the main loop.
void install_termination_handler() {
    (void)SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    struct sigaction action = {};
    action.sa_handler = on_termination;
    sigemptyset(&action.sa_mask);
    for (const int sig : {SIGTERM, SIGINT, SIGHUP}) {
        (void)sigaction(sig, &action, nullptr);
    }
}

/// Started by a desktop launcher or the Finder, stderr is the system log's socket or /dev/null,
/// where nobody looks for it. A terminal, a pipe or a file the user redirected to stays.
bool stderr_leads_nowhere() {
    struct stat st = {};
    if (fstat(STDERR_FILENO, &st) != 0) {
        return true;
    }
    if (S_ISSOCK(st.st_mode)) {
        return true;
    }
    return S_ISCHR(st.st_mode) && isatty(STDERR_FILENO) == 0;
}

#endif

}  // namespace

void setup_diagnostics() {
#if defined(_WIN32)
    // Redirected output (open1602.exe 2> log.txt, a test runner) stays where it goes.
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err != nullptr && err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN) {
        // nothing to change
    } else if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        // Started from cmd or PowerShell: the messages appear there, in UTF-8.
        SetConsoleOutputCP(CP_UTF8);
        FILE* stream = nullptr;
        (void)freopen_s(&stream, "CONOUT$", "w", stdout);
        (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    } else {
        log_to_file();  // double-clicked
    }
#elif !defined(O1602_EXPERIMENTAL_VITA)
    if (stderr_leads_nowhere()) {
        log_to_file();
    }
    install_termination_handler();
#endif
    std::set_terminate(on_terminate);
#if !defined(O1602_SANITIZED) && !defined(O1602_EXPERIMENTAL_VITA)
    install_crash_handler();  // with sanitizers, they report crashes themselves, and better
#endif
}

bool termination_requested() {
    return g_termination != 0;
}

void report_exit(int code) {
    if (g_log_file) {
        std::fprintf(stderr, "Open1602 ended, exit code %d\n", code);
    }
}

void set_dialogs(bool on) {
    g_dialogs = on;
}

void report_fatal(std::string_view message) {
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::fatal(std::string(message).c_str());
    return;
#endif
    const std::string text(message);
    std::fprintf(stderr, "error: %s\n", text.c_str());
    if (!g_dialogs) {
        return;
    }
#if defined(_WIN32)
    (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open1602", text.c_str(), nullptr);
#else
    // Started from a terminal, the message is there already; from the Finder or a desktop
    // launcher, stderr leads nowhere and the program would end without a word. SDL shows the box
    // without a window and before SDL_Init.
    if (isatty(fileno(stderr)) == 0) {
        (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Open1602", text.c_str(), nullptr);
    }
#endif
}

}  // namespace o1602::app
