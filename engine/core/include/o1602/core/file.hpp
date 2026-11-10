// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::core {

using Bytes = std::vector<std::uint8_t>;

/// A path from UTF-8 text (SDL, the command line, settings files). On Windows the narrow
/// constructor of `std::filesystem::path` reads the ANSI code page instead, which turns a user
/// named "Jürgen" into "JÃ¼rgen"; elsewhere the bytes stay as they are.
std::filesystem::path path_from_utf8(std::string_view text);

/// A path as UTF-8 text for messages, SDL and settings files. Never throws: on Windows
/// `path::string()` does for characters outside the ANSI code page.
std::string path_to_utf8(const std::filesystem::path& path);

/// The value of an environment variable as UTF-8 text; nothing if it is unset or empty.
std::optional<std::string> environment_variable(const char* name);

/// Largest file read_file reads (the biggest files of the game, its videos, are far smaller).
inline constexpr std::uint64_t kMaxReadSize = std::uint64_t{1} << 30;

/// Reads a whole regular file of at most kMaxReadSize bytes into memory.
Result<Bytes> read_file(const std::filesystem::path& path);

/// The start of a regular file: its first `max_bytes` bytes (fewer if it is shorter).
struct FileHead {
    Bytes bytes;
    std::uint64_t size = 0;  ///< of the whole file
};
/// Reads the first `max_bytes` bytes of a regular file, and its size (a list of savegames needs
/// only their heads).
Result<FileHead> read_file_head(const std::filesystem::path& path, std::size_t max_bytes);

/// Writes a whole file (creates or truncates).
Status write_file(const std::filesystem::path& path, std::span<const std::uint8_t> data);

/// Writes a whole file so that `path` holds either the old or the complete new content, never
/// half of it (savegames): into `path`.tmp, onto the disk, then renamed over `path`.
Status write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> data);

/// Resolves a relative path below `root` ignoring the case of every path component.
///
/// The original game was written for Windows (case-insensitive file system) and references
/// files with inconsistent casing, e.g. "mgfx\stadtfld.bsh" while the file is "MGFX/Stadtfld.bsh".
/// Both '/' and '\\' are accepted as separators in `relative`.
/// An exact match is tried first, so the common case does not touch the directory listing.
/// Absolute paths and ".." components find nothing: the result always lies inside `root`.
std::optional<std::filesystem::path> find_case_insensitive(const std::filesystem::path& root,
                                                           std::string_view relative);

}  // namespace o1602::core
