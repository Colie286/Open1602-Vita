// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool: what the command files share (argument parsing, error output, the palette).

#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "o1602/assets/palette.hpp"
#include "o1602/core/result.hpp"

namespace o1602::tool {

namespace fs = std::filesystem;

/// The options that take no value: the word after one of them is a positional argument
/// ("render-world --owners a.gam out.png").
inline constexpr std::string_view kFlags[] = {
    "--buildings",       "--no-template", "--original-data", "--original-limits", "--original-flags",
    "--original-sheets", "--owners",      "--production",    "--sheet",           "--together",
    "--via-own",         "--walkers",     "--wall",          "--wares",           "--without-original",
};
/// The options whose value is a whole number.
inline constexpr std::string_view kNumbers[] = {
    "--anim",    "--count",  "--every",    "--first", "--gold-islands", "--island", "--level", "--minutes", "--period",
    "--players", "--repeat", "--rotation", "--seed",  "--simulate",     "--stars",  "--time",  "--wav",     "--zoom",
};

/// A whole number that is the whole text ("6O" is none, not 6).
[[nodiscard]] inline std::optional<std::size_t> parse_number(std::string_view text) {
    std::size_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

/// Simple "--name value" / "--flag" option lookup. Problems with the arguments (an option without
/// its value, a number that is none) are collected in `problems`; main() refuses to run with them.
struct Args {
    std::vector<std::string_view> positional;
    std::map<std::string_view, std::string_view> options;
    std::vector<std::string> problems;

    Args(int argc, char** argv) {
        const auto is_flag = [](std::string_view a) {
            return std::find(std::begin(kFlags), std::end(kFlags), a) != std::end(kFlags);
        };
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (!arg.starts_with("--")) {
                positional.push_back(arg);
            } else if (is_flag(arg)) {
                options[arg] = std::string_view();
            } else if (i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--")) {
                options[arg] = argv[++i];
            } else {
                problems.push_back(std::string(arg) + " needs a value");
            }
        }
        for (const std::string_view name : kNumbers) {
            if (const auto it = options.find(name); it != options.end() && !parse_number(it->second)) {
                problems.push_back(std::string(name) + ": '" + std::string(it->second) + "' is not a whole number");
            }
        }
    }

    [[nodiscard]] bool flag(std::string_view name) const { return options.contains(name); }

    [[nodiscard]] std::optional<std::size_t> number(std::string_view name) const {
        const auto it = options.find(name);
        return it == options.end() ? std::nullopt : parse_number(it->second);
    }

    /// "--name A<sep>B" as two numbers, e.g. "--tile 120,80" or "--size 1920x1080".
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> pair(std::string_view name,
                                                                            char separator) const {
        const auto it = options.find(name);
        if (it == options.end()) {
            return std::nullopt;
        }
        const std::string_view v = it->second;
        const auto sep = v.find(separator);
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }
        std::int32_t a = 0;
        std::int32_t b = 0;
        if (std::from_chars(v.data(), v.data() + sep, a).ec != std::errc{} ||
            std::from_chars(v.data() + sep + 1, v.data() + v.size(), b).ec != std::errc{}) {
            return std::nullopt;
        }
        return std::pair{a, b};
    }
};

/// Prints "error: <message>" and returns exit code 1.
int fail(const core::Error& error);

/// Prints the usage and returns exit code 2.
int usage();

/// The palette of --palette, or $ANNO1602_DIR/TOOLGFX/STADTFLD.COL.
core::Result<assets::Palette> load_palette(const Args& args);

// bench.cpp
int cmd_bench(const Args& args);

// formats.cpp
int cmd_chunks(const Args& args);
int cmd_cod(const Args& args);
int cmd_cod_dump(const Args& args);
int cmd_bsh_info(const Args& args);
int cmd_bsh_export(const Args& args);

// savegames.cpp
int cmd_savegames(const Args& args);
int cmd_state(const Args& args);
int cmd_islands(const Args& args);
int cmd_new_world(const Args& args);
int cmd_export(const Args& args);

// render.cpp
int cmd_render_island(const Args& args);
int cmd_render_world(const Args& args);

// scenario.cpp
int cmd_scenario(const Args& args);

// templates.cpp
int cmd_template_stats(const Args& args);

// video.cpp
int cmd_smk(const Args& args);
int cmd_bk2(const Args& args);
int cmd_webm(const Args& args);

// mods.cpp
int cmd_check_mod(const Args& args);
int cmd_export_island(const Args& args);

}  // namespace o1602::tool
