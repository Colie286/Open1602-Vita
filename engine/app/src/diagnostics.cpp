// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnostics.hpp"
#ifdef O1602_EXPERIMENTAL_VITA
#include "o1602/platform/vita.hpp"
#endif

#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>
#include <filesystem>

#include "o1602/core/file.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <SDL3/SDL_messagebox.h>
#include <unistd.h>
#endif

namespace o1602::app {

namespace {

bool g_dialogs = true;

}  // namespace

void setup_diagnostics() {
#if defined(_WIN32)
    // Redirected output (open1602.exe 2> log.txt, a test runner) stays where it goes.
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err != nullptr && err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        // Started from cmd or PowerShell: the messages appear there, in UTF-8.
        SetConsoleOutputCP(CP_UTF8);
        FILE* stream = nullptr;
        (void)freopen_s(&stream, "CONOUT$", "w", stdout);
        (void)freopen_s(&stream, "CONOUT$", "w", stderr);
        return;
    }
    // Double-clicked: into the user's data folder (MapViewer::data_folder(), SDL makes it).
    char* folder = SDL_GetPrefPath(nullptr, "Open1602");
    if (folder == nullptr) {
        return;
    }
    const std::filesystem::path log = core::path_from_utf8(folder) / "open1602.log";
    SDL_free(folder);
    FILE* stream = nullptr;
    if (_wfreopen_s(&stream, log.c_str(), L"w", stderr) == 0) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);  // nothing is lost when the program crashes
    }
#endif
}

void set_dialogs(bool on) {
    g_dialogs = on;
}

void report_fatal(std::string_view message) {
#ifdef O1602_EXPERIMENTAL_VITA
    platform::vita::fatal(std::string(message).c_str());
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
