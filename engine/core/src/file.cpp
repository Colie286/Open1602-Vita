// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/core/profile.hpp"
#include "o1602/core/file.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace o1602::core {

namespace fs = std::filesystem;

fs::path path_from_utf8(std::string_view text) {
#if defined(_WIN32)
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
#else
    return fs::path(std::string(text));
#endif
}

std::string path_to_utf8(const fs::path& path) {
#if defined(_WIN32)
    try {
        const std::u8string text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    } catch (const std::exception&) {
        return "(path not valid Unicode)";
    }
#else
    return path.string();
#endif
}

std::optional<std::string> environment_variable(const char* name) {
#if defined(_WIN32)
    const std::wstring wide_name = path_from_utf8(name).wstring();
    const wchar_t* value = _wgetenv(wide_name.c_str());
    if (value == nullptr || *value == L'\0') {
        return std::nullopt;
    }
    return path_to_utf8(fs::path(value));
#else
    const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe)
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

Result<Bytes> read_file(const fs::path& path) {
    profile::Scope timer(profile::FileIO);
    // A directory opens as a stream on some platforms and reports a size of 2^63 - 1.
    std::error_code ec;
    if (fs::exists(path, ec) && !fs::is_regular_file(path, ec)) {
        return make_error(ErrorCode::IoError, "not a file: " + path_to_utf8(path));
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return make_error(ErrorCode::NotFound, "cannot open file: " + path_to_utf8(path));
    }
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0) {
        return make_error(ErrorCode::IoError, "cannot determine size: " + path_to_utf8(path));
    }
    if (static_cast<std::uint64_t>(end) > kMaxReadSize) {
        return make_error(ErrorCode::IoError, "file too large: " + path_to_utf8(path));
    }
    in.seekg(0, std::ios::beg);
    Bytes data(static_cast<std::size_t>(end));
    if (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
        return make_error(ErrorCode::IoError, "cannot read file: " + path_to_utf8(path));
    }
    return data;
}

Result<FileHead> read_file_head(const fs::path& path, std::size_t max_bytes) {
    std::error_code ec;
    if (fs::exists(path, ec) && !fs::is_regular_file(path, ec)) {
        return make_error(ErrorCode::IoError, "not a file: " + path_to_utf8(path));
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return make_error(ErrorCode::NotFound, "cannot open file: " + path_to_utf8(path));
    }
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0) {
        return make_error(ErrorCode::IoError, "cannot determine size: " + path_to_utf8(path));
    }
    in.seekg(0, std::ios::beg);
    FileHead head;
    head.size = static_cast<std::uint64_t>(end);
    head.bytes.resize(static_cast<std::size_t>(std::min<std::uint64_t>(head.size, max_bytes)));
    if (!head.bytes.empty() &&
        !in.read(reinterpret_cast<char*>(head.bytes.data()), static_cast<std::streamsize>(head.bytes.size()))) {
        return make_error(ErrorCode::IoError, "cannot read file: " + path_to_utf8(path));
    }
    return head;
}

Status write_file(const fs::path& path, std::span<const std::uint8_t> data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return make_error(ErrorCode::IoError, "cannot create file: " + path_to_utf8(path));
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    // The last part of the buffer reaches the disk only now; a full disk shows up here.
    out.close();
    if (out.fail()) {
        return make_error(ErrorCode::IoError, "cannot write file: " + path_to_utf8(path));
    }
    return Ok{};
}

namespace {

/// Writes and forces the data onto the disk before returning, so that a rename after it cannot
/// replace a good file with one the system has not written yet.
Status write_durable(const fs::path& path, std::span<const std::uint8_t> data) {
#if defined(_WIN32)
    std::FILE* f = _wfopen(path.c_str(), L"wb");
#else
    std::FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (f == nullptr) {
        return make_error(ErrorCode::IoError, "cannot create file: " + path_to_utf8(path));
    }
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fflush(f) == 0 && ok;
#if defined(_WIN32)
    ok = _commit(_fileno(f)) == 0 && ok;
#else
    ok = fsync(fileno(f)) == 0 && ok;
#endif
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        return make_error(ErrorCode::IoError, "cannot write file: " + path_to_utf8(path));
    }
    return Ok{};
}

/// Renames `from` to `to`, replacing it, and has the new name on the disk before returning, so
/// that a power loss right after it does not leave only the .tmp file (DS-6).
void rename_durable(const fs::path& from, const fs::path& to, std::error_code& ec) {
#if defined(_WIN32)
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }
#else
    fs::rename(from, to, ec);
    if (ec) {
        return;
    }
    // The directory holds the name. Some file systems cannot flush a directory; the rename stands.
    fs::path folder = to.parent_path();
    if (folder.empty()) {
        folder = ".";
    }
    if (const int fd = ::open(folder.c_str(), O_RDONLY); fd >= 0) {
        (void)::fsync(fd);
        ::close(fd);
    }
#endif
}

}  // namespace

Status write_file_atomic(const fs::path& path, std::span<const std::uint8_t> data) {
    fs::path temp = path;
    temp += ".tmp";
    std::error_code ec;
    if (auto status = write_durable(temp, data); !status) {
        fs::remove(temp, ec);
        return status;
    }
    rename_durable(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        return make_error(ErrorCode::IoError, "cannot write " + path_to_utf8(path) + ": " + ec.message());
    }
    return Ok{};
}

namespace {

bool iequals(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

}  // namespace

std::optional<fs::path> find_case_insensitive(const fs::path& root, std::string_view relative) {
    std::string normalized(relative);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    // Only paths inside `root`: names come from data files and mods, which must not reach elsewhere.
    if (normalized.starts_with('/') || path_from_utf8(normalized).has_root_name() ||
        ("/" + normalized + "/").find("/../") != std::string::npos) {
        return std::nullopt;
    }

    std::error_code ec;
    if (fs::path exact = root / path_from_utf8(normalized); fs::exists(exact, ec)) {
        return exact;
    }

    fs::path current = root;
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t end = std::min(normalized.find('/', start), normalized.size());
        const std::string_view component(normalized.data() + start, end - start);
        start = end + 1;
        if (component.empty() || component == ".") {
            continue;
        }

        bool found = false;
        for (fs::directory_iterator it(current, ec), last; !ec && it != last; it.increment(ec)) {
            if (iequals(path_to_utf8(it->path().filename()), component)) {
                current = it->path();
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }
    return current;
}

}  // namespace o1602::core
